# MyNVS

ESP-IDF NVS 的现代 C++ 便捷封装。在原生 `nvs_get_*` / `nvs_set_*` 之上提供统一类型的
`read` / `write` 接口，并用引用计数的命名空间槽位管理器复用 `nvs_handle_t`。

---

## 特性

- **统一接口**：一份 `read` / `write` 覆盖标量、`std::string`、`std::vector`、`std::array`、原始 blob。
- **类型安全**：基于 C++20 concept 约束，编译期拒绝不支持的类型；`bool` / 枚举 / 整数 / 浮点分别走正确分支。
- **零字符串拷贝**：`key` 用 `std::string_view` 接收，内部 `SafeKey` 一次性规范化为带 `'\0'` 的 C 串。
- **命名空间复用**：相同 `(partition, namespace)` 多次打开共享同一 `nvs_handle_t`，引用计数归零才 `commit + close`。
- **线程安全**：全局互斥保护槽位池；每个槽位独立互斥保护 `handle` 上的读写。
- **RAII**：`Guard` 对象在构造时一次性完成"加锁 + 句柄有效性检查 + 读写模式检查"。
- **返回值透明**：所有 API 返回 `esp_err_t`，不做二次封装。

---

## 文件结构

```
my_nvs/
├── include/my_nvs.h        // MyNVS 类 + 模板实现
├── src/my_nvs_manager.h    // 槽位管理器 & my_nvs_t 定义
├── src/nvs_manager.cc      // 管理器实现：init / open / close
├── src/my_nvs.cc           // 非模板成员实现（字符串 / blob / find / erase / commit）
└── test/                   // 测试用例
```

---

## 快速开始

```cpp
#include "my_nvs.h"

MyNVS nvs("my_ns", NVS_READWRITE);   // 使用默认 "nvs" 分区
// 或 MyNVS nvs("custom_part", "my_ns", NVS_READWRITE);

// 字符串
esp_err_t err = nvs.write("name", std::string("ESP32"));
std::string name;
err = nvs.read("name", name);        // 读到 name 里，不含 '\0'

// 整数 / 浮点
nvs.write("count",  int32_t{42});
nvs.write("scale",  1.5f);

int32_t c = 0;
nvs.read("count", c);

// 字节容器
std::vector<uint8_t> v = {1, 2, 3};
nvs.write("v", v);

std::vector<uint8_t> v2;
nvs.read("v", v2);                   // 自动 resize 到实际长度

std::array<uint8_t, 3> a = {1, 2, 3};
nvs.write("a", a);                   // 固定容器，长度必须一致

// 原始 blob（nullptr 只查长度）
size_t len = 0;
nvs.read("v", (void*)nullptr, &len);

// 查询 / 删除 / 提交
nvs_type_t type;
if (nvs.find("name", type) == ESP_OK) {
    // type == NVS_TYPE_STR
}
nvs.erase_key("name");
nvs.erase_all();
nvs.commit();
```

---

## API 参考

### 构造 / 析构

| 接口 | 说明 |
| --- | --- |
| `MyNVS(name_space, mode = NVS_READONLY)` | 使用 `"nvs"` 分区 |
| `MyNVS(partition, name_space, mode = NVS_READONLY)` | 指定分区 |
| `~MyNVS()` | 引用计数减一；归零时 `commit + close` |

> 构造失败时 `nvs_ == nullptr`，后续操作统一返回 `ESP_ERR_INVALID_STATE`。

### 通用操作

| 接口 | 说明 |
| --- | --- |
| `find(key, nvs_type_t&)` | 查询 key 是否存在及其类型 |
| `erase_key(key)` | 删除单个 key |
| `erase_all()` | 清空当前命名空间 |
| `commit()` | 提交挂起的写入 |

### 读 / 写

| 重载 | 底层 | 说明 |
| --- | --- | --- |
| `read(key, std::string&)` | `nvs_get_str` | 两遍读取：先取长度再分配，读后去掉 `'\0'` |
| `write(key, const std::string&)` | `nvs_set_str` | |
| `read(key, void*, size_t*)` | `nvs_get_blob` | `value == nullptr` 时仅返回所需长度（WinAPI 风格） |
| `write(key, const void*, size_t)` | `nvs_set_blob` | |
| `read(key, ResizableByteBuffer&)` | `nvs_get_blob` | 自动 `resize` 到实际长度 |
| `read(key, FixedByteBuffer&)` | `nvs_get_blob` | 长度不匹配返回 `ESP_ERR_NVS_INVALID_LENGTH` |
| `write(key, ByteBuffer&)` | `nvs_set_blob` | 两种容器共用实现 |
| `read(key, T&)` / `write(key, T)` | 按宽度/符号分派 | 标量：`bool` / `char` / `enum(1B)` / 整数 / `float` / `double` |

**标量映射表：**

| C++ 类型 | NVS 底层 |
| --- | --- |
| `bool` | `nvs_get/set_u8`（0/1） |
| `char` / `enum : uint8_t` | `nvs_get/set_u8`（位模式） |
| `int8_t / int16_t / int32_t / int64_t` | `nvs_get/set_i8/i16/i32/i64` |
| `uint8_t / uint16_t / uint32_t / uint64_t` | `nvs_get/set_u8/u16/u32/u64` |
| `float` / `double` | `nvs_get/set_u32/u64`（**位模式**，非数值转换） |

---

## 设计说明

### 槽位池 & 引用计数

`MyNVS_Manager` 是进程内单例，持有固定容量的槽位数组（大小由 `CONFIG_MAX_NAMESPACE` 决定）。
每次打开 `(partition, namespace)` 时：

1. 在槽位中查找 `(partition, namespace)` 完全一致的项 → **复用并 `ref++`**；
2. 否则找空闲槽位 → `nvs_open_from_partition` 真正打开，`ref = 1`；
3. 只读槽位**不允许**升级为读写（避免并发下模式切换）；
4. 最后一个引用析构时才 `nvs_commit + nvs_close`。

### SafeKey

NVS 要求 key 以 `'\0'` 结尾且不超过 15 字节；`std::string_view` 不保证这两点。
`SafeKey` 在构造时拷贝到本地定长缓冲区、补齐 `'\0'`、超长则截断并告警，保证传给
底层 C API 的指针总是合法的。

### Guard

`Guard` 是一个轻量 RAII：

```cpp
Guard g(nvs_, /*need_write=*/true);
if (!g.ok) return ESP_ERR_INVALID_STATE;   // 未打开 / 只读 / 已失效
// ... 使用 g.nvs->handle ...
```

构造时一次性完成 **加锁 + 句柄检查 + 模式检查**，作用域结束时自动解锁。

---

## 注意事项

- **枚举必须显式指定为 1 字节的uint8_t**（如 `enum class E : uint8_t { ... };`），否则 `static_assert` 报错。
- `float` / `double` 以**位模式**存储（`uint32_t` / `uint64_t`），不做数值转换；请勿与整数 key 混用同一 key 名。
- **key 名超长会被截断并告警**，但不会报错（与 ESP-IDF 原生 `nvs_*` 行为一致）。
- **只读命名空间无法升级为读写**：如果先以 `NVS_READONLY` 打开，之后同一 `(partition, namespace)` 请求 `NVS_READWRITE` 会失败，需先关闭所有引用。
- **并发打开多个不同命名空间**的总数受 `CONFIG_MAX_NAMESPACE` 限制；测试中应尽量使用独立命名空间并及时让 `MyNVS` 析构。
- 构造失败**不会抛异常**，需要通过后续 `read` / `write` 返回的 `ESP_ERR_INVALID_STATE` 感知。

---

## 测试

```
idf.py build flash monitor

# pytest --target esp32c3 --embedded-services esp,idf -v
```

需进入组件 `test/` 目录按 ESP-IDF 单元测试流程运行。