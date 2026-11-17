/**
 * @file    my_nvs.h
 * @brief   NVS 便捷封装：在 ESP-IDF 的 nvs_get_* / nvs_set_* 之上提供
 *          统一类型的 read/write 接口，支持标量、std::string、std::vector、
 *          std::array 以及原始 void* blob。
 *
 * 设计要点：
 *   - key 统一用 std::string_view 接收，内部 SafeKey 规范化并补 '\0'；
 *   - std::string 走独立的非模板重载（走 nvs_get_str / nvs_set_str）；
 *   - 字节容器用 concept 区分 ResizableByteBuffer 与 FixedByteBuffer；
 *   - 标量用 SupportedType concept 约束，内部按宽度与符号分派；
 *   - float / double 以位模式（uint32_t / uint64_t）存取，不做数值转换。
 */

#ifndef _MY_NVS_H_
#define _MY_NVS_H_

#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <tuple>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <cstdint>
#include <concepts>
#include <type_traits>
#include "esp_log.h"
#include "nvs_flash.h"
#include "my_nvs_manager.h"

#define PARTITION_LENGTH    15   ///< 分区名最大长度（NVS 限制）
#define NAMESPACE_LENGTH    15   ///< 命名空间最大长度（NVS 限制）
#define KEY_LENGTH          15   ///< 键名最大长度（NVS 限制）

// ======================================================
// 类型概念
// ======================================================

template<typename T> concept BoolType       = std::is_same_v<T, bool>;                              ///< 布尔
template<typename T> concept EnumType       = std::is_enum_v<T>;                                    ///< 枚举
template<typename T> concept CharType       = std::is_same_v<T, char>;                              ///< 单字符
template<typename T> concept IntegerType    = std::is_integral_v<T> && !std::is_same_v<T, bool>;    ///< 整数（不含 bool）
template<typename T> concept FloatingType   = std::is_floating_point_v<T>;                          ///< 浮点
template<typename T> concept StringType     = std::is_same_v<T, std::string>;                       ///< 字符串

/// 标量类型集合：std::string 走独立非模板重载，故不纳入 SupportedType
template<typename T> concept SupportedType  =
    BoolType<T> || EnumType<T> || CharType<T> || IntegerType<T> || FloatingType<T>;

/**
 * @brief 可 resize 的字节容器（如 std::vector<uint8_t>）
 *        - 元素 1 字节
 *        - 排除 char（走字符串路径）与 bool（vector<bool> 无 data()）
 *        - 必须有 data() / size() / resize()
 */
template <typename C>
concept ResizableByteBuffer = requires(C c, size_t n) {
    typename C::value_type;
    requires sizeof(typename C::value_type) == 1;
    requires !std::is_same_v<typename C::value_type, char>;
    requires !std::is_same_v<typename C::value_type, bool>;
    { c.data() } -> std::convertible_to<void*>;
    { c.size() } -> std::convertible_to<size_t>;
    c.resize(n);
};

/**
 * @brief 固定大小的字节容器（如 std::array<uint8_t, N>）
 *        - 元素 1 字节且非 char / bool
 *        - 没有 resize()
 *        - 有 std::tuple_size 特化（std::array 满足，std::span 不满足）
 */
template <typename C>
concept FixedByteBuffer = requires {
    typename C::value_type;
    requires sizeof(typename C::value_type) == 1;
    requires !std::is_same_v<typename C::value_type, char>;
    requires !std::is_same_v<typename C::value_type, bool>;
    requires (!ResizableByteBuffer<C>);
    { std::tuple_size<C>::value } -> std::convertible_to<size_t>;
};

template<class> inline constexpr bool always_false = false;   ///< 用于 static_assert 的依赖失败模板

// ======================================================
// MyNVS
// ======================================================

class MyNVS {
public:
    explicit MyNVS(std::string_view name_space, nvs_open_mode_t mode = NVS_READONLY) 
        : MyNVS("nvs", name_space, mode) {}
    explicit MyNVS(std::string_view partition, std::string_view name_space, nvs_open_mode_t mode = NVS_READONLY);
    ~MyNVS();

    // ---- 查询 / 删除 / 提交 ----
    esp_err_t find(std::string_view key, nvs_type_t& type);   ///< 查询 key 是否存在及其类型
    esp_err_t erase_key(std::string_view key);                ///< 删除单个 key
    esp_err_t erase_all();                                    ///< 清空当前命名空间
    esp_err_t commit();                                       ///< 提交挂起的写入

    // ---- 字符串（非模板，nvs_get_str / nvs_set_str）----
    esp_err_t read (std::string_view key, std::string& v);
    esp_err_t write(std::string_view key, const std::string& v);

    // ---- 原始 blob（非模板，nvs_get_blob / nvs_set_blob）----
    // read 时若 value 为 nullptr，仅返回所需长度到 *n（WinAPI 风格）
    esp_err_t read (std::string_view key, void* v, size_t* n);
    esp_err_t write(std::string_view key, const void* v, size_t n);

    // ---- 字节容器（模板）----
    /// 可 resize 容器：先查长度 → resize → 读数据
    template <ResizableByteBuffer C>
    esp_err_t read (std::string_view key, C& value);

    /// 固定容器：NVS 长度必须 == value.size()，否则返回 ESP_ERR_NVS_INVALID_LENGTH
    template <FixedByteBuffer C>
    esp_err_t read (std::string_view key, C& value);

    /// 写入：两种容器共用一份实现
    template <typename C>
        requires ResizableByteBuffer<C> || FixedByteBuffer<C>
    esp_err_t write(std::string_view key, const C& value);

    // ---- 标量模板（整数 / 浮点 / bool / char / enum）----
    template<SupportedType T>
    esp_err_t read (std::string_view key, T& v);
    template<SupportedType T>
    esp_err_t write(std::string_view key, const T& v);

private:
    /**
     * @brief RAII 键名守卫：把任意长度的 string_view 规范化为
     *        最长 KEY_LENGTH 字节、以 '\0' 结尾的 C 字符串。
     *
     * 注意：c_str()/key 指针只在本对象存活期间有效，不要保存到函数外。
     */
    struct SafeKey {
        char key[KEY_LENGTH + 1];   ///< 规范化后的 key，保证 '\0' 结尾

        explicit SafeKey(std::string_view k) {
            key[0] = '\0';
            if (k.empty()) return;

            // string_view 不保证 '\0' 结尾，必须拷贝到本地缓冲区再补 '\0'
            const size_t n = std::min(k.size(), static_cast<size_t>(KEY_LENGTH));
            std::memcpy(key, k.data(), n);
            key[n] = '\0';

            if (k.size() > KEY_LENGTH) {
                // %.*s 用 precision 截断打印，不依赖原 key 的 '\0' 结尾
                ESP_LOGW("MyNVS", "key 过长已截断: %.*s -> %s",
                         static_cast<int>(k.size()), k.data(), key);
            }
        }

        bool empty() const noexcept { return key[0] == '\0'; }   ///< 是否为空 key
        explicit operator bool() const noexcept { return !empty(); }
        const char* c_str() const noexcept { return key; }
    };

    /**
     * @brief 统一锁 + 状态检查。
     *        构造后 ok == false 时调用方直接返回 ESP_ERR_INVALID_STATE。
     */
    struct Guard {
        my_nvs_t*                    nvs;
        std::unique_lock<std::mutex> lock;
        bool                         ok = false;

        Guard(my_nvs_t* n, bool need_write) : nvs(n) {
            if (!nvs) return;
            lock = std::unique_lock<std::mutex>(nvs->mutex);
            if (nvs->handle == 0) return;
            if (need_write && nvs->mode != NVS_READWRITE) return;
            ok = true;
        }
    };

    my_nvs_t*       nvs_{nullptr};       ///< 当前命名空间槽位
    MyNVS_Manager*  manager_{nullptr};   ///< 管理器单例
};

// ======================================================
// 标量模板实现
// ======================================================

template <SupportedType T>
esp_err_t MyNVS::read(std::string_view key, T& value)
{
    SafeKey sk(key);
    if (!sk) { ESP_LOGE("MyNVS", "键名为空"); return ESP_ERR_INVALID_ARG; }

    Guard guard(nvs_, /*need_write=*/false);
    if (!guard.ok) { ESP_LOGE("MyNVS", "NVS 未打开或实例已失效"); return ESP_ERR_INVALID_STATE; }

    auto handle = nvs_->handle;

    if constexpr (BoolType<T> || CharType<T> || EnumType<T>) {
        // bool/char/enum 统一按 u8 存取；enum 必须显式指定底层为 1 字节
        static_assert(sizeof(T) == 1, "bool/char/enum 必须为 1 字节，枚举请显式指定 : uint8_t");
        uint8_t tmp = 0;
        auto err = nvs_get_u8(handle, sk.c_str(), &tmp);
        if (err == ESP_OK) {
            if constexpr (BoolType<T>) value = (tmp != 0);
            else                       std::memcpy(&value, &tmp, sizeof(T));
        }
        return err;

    } else if constexpr (IntegerType<T>) {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                      "仅支持 1/2/4/8 字节整数");

        // 用精确宽度类型 U 中转：规避 T=long 等指针类型不匹配 + 严格别名
        using U = std::conditional_t<std::is_signed_v<T>,
                    std::conditional_t<sizeof(T) == 1, int8_t,
                    std::conditional_t<sizeof(T) == 2, int16_t,
                    std::conditional_t<sizeof(T) == 4, int32_t, int64_t>>>,
                    std::conditional_t<sizeof(T) == 1, uint8_t,
                    std::conditional_t<sizeof(T) == 2, uint16_t,
                    std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>>>>;
        U tmp = 0;
        esp_err_t err;
        if constexpr (sizeof(T) == 1) {
            if constexpr (std::is_signed_v<T>) err = nvs_get_i8 (handle, sk.c_str(), &tmp);
            else                               err = nvs_get_u8 (handle, sk.c_str(), &tmp);
        } else if constexpr (sizeof(T) == 2) {
            if constexpr (std::is_signed_v<T>) err = nvs_get_i16(handle, sk.c_str(), &tmp);
            else                               err = nvs_get_u16(handle, sk.c_str(), &tmp);
        } else if constexpr (sizeof(T) == 4) {
            if constexpr (std::is_signed_v<T>) err = nvs_get_i32(handle, sk.c_str(), &tmp);
            else                               err = nvs_get_u32(handle, sk.c_str(), &tmp);
        } else {
            if constexpr (std::is_signed_v<T>) err = nvs_get_i64(handle, sk.c_str(), &tmp);
            else                               err = nvs_get_u64(handle, sk.c_str(), &tmp);
        }
        if (err == ESP_OK) std::memcpy(&value, &tmp, sizeof(T));
        return err;

    } else if constexpr (FloatingType<T>) {
        // float/double 按位模式存取：先读整数，再 memcpy 位模式到浮点
        using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
        static_assert(sizeof(T) == sizeof(U), "浮点类型必须是 4 或 8 字节");
        U tmp = 0;
        esp_err_t err;
        if constexpr (sizeof(T) == 4) err = nvs_get_u32(handle, sk.c_str(), &tmp);
        else                          err = nvs_get_u64(handle, sk.c_str(), &tmp);
        if (err == ESP_OK) std::memcpy(&value, &tmp, sizeof(T));
        return err;

    } else {
        static_assert(always_false<T>, "暂不支持该类型");
        return ESP_ERR_NOT_SUPPORTED;
    }
}

template <SupportedType T>
esp_err_t MyNVS::write(std::string_view key, const T& value)
{
    SafeKey sk(key);
    if (!sk) { ESP_LOGE("MyNVS", "键名为空"); return ESP_ERR_INVALID_ARG; }

    Guard guard(nvs_, /*need_write=*/true);
    if (!guard.ok) { ESP_LOGE("MyNVS", "NVS 未打开或只读"); return ESP_ERR_INVALID_STATE; }

    auto handle = nvs_->handle;

    if constexpr (BoolType<T>) {
        return nvs_set_u8(handle, sk.c_str(), value ? 1 : 0);

    } else if constexpr (CharType<T> || EnumType<T>) {
        static_assert(sizeof(T) == 1, "char/enum 必须为 1 字节，枚举请显式指定 : uint8_t");
        uint8_t tmp;
        std::memcpy(&tmp, &value, sizeof(T));
        return nvs_set_u8(handle, sk.c_str(), tmp);

    } else if constexpr (IntegerType<T>) {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                      "仅支持 1/2/4/8 字节整数");
        if constexpr (sizeof(T) == 1) {
            if constexpr (std::is_signed_v<T>) return nvs_set_i8 (handle, sk.c_str(), static_cast<int8_t>(value));
            else                               return nvs_set_u8 (handle, sk.c_str(), static_cast<uint8_t>(value));
        } else if constexpr (sizeof(T) == 2) {
            if constexpr (std::is_signed_v<T>) return nvs_set_i16(handle, sk.c_str(), static_cast<int16_t>(value));
            else                               return nvs_set_u16(handle, sk.c_str(), static_cast<uint16_t>(value));
        } else if constexpr (sizeof(T) == 4) {
            if constexpr (std::is_signed_v<T>) return nvs_set_i32(handle, sk.c_str(), static_cast<int32_t>(value));
            else                               return nvs_set_u32(handle, sk.c_str(), static_cast<uint32_t>(value));
        } else {
            if constexpr (std::is_signed_v<T>) return nvs_set_i64(handle, sk.c_str(), static_cast<int64_t>(value));
            else                               return nvs_set_u64(handle, sk.c_str(), static_cast<uint64_t>(value));
        }

    } else if constexpr (FloatingType<T>) {
        using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
        static_assert(sizeof(T) == sizeof(U), "浮点类型必须是 4 或 8 字节");
        U tmp;
        std::memcpy(&tmp, &value, sizeof(T));
        if constexpr (sizeof(T) == 4) return nvs_set_u32(handle, sk.c_str(), tmp);
        else                          return nvs_set_u64(handle, sk.c_str(), tmp);

    } else {
        static_assert(always_false<T>, "暂不支持该类型");
        return ESP_ERR_NOT_SUPPORTED;
    }
}

// ======================================================
// 字节容器模板实现
// ======================================================

/// 可 resize 容器：读时先取长度、resize 到实际长度、再读数据
template <ResizableByteBuffer C>
esp_err_t MyNVS::read(std::string_view key, C& value)
{
    SafeKey sk(key);
    if (!sk) { ESP_LOGE("MyNVS", "键名为空"); return ESP_ERR_INVALID_ARG; }

    Guard guard(nvs_, /*need_write=*/false);
    if (!guard.ok) { ESP_LOGE("MyNVS", "NVS 未打开或实例已失效"); return ESP_ERR_INVALID_STATE; }

    size_t len = 0;
    auto err = nvs_get_blob(nvs_->handle, sk.c_str(), nullptr, &len);
    if (err != ESP_OK) return err;

    value.resize(len);
    if (len == 0) return ESP_OK;

    return nvs_get_blob(nvs_->handle, sk.c_str(), value.data(), &len);
}

/// 固定容器：NVS 长度必须恰好等于 value.size()，否则返回 ESP_ERR_NVS_INVALID_LENGTH
template <FixedByteBuffer C>
esp_err_t MyNVS::read(std::string_view key, C& value)
{
    SafeKey sk(key);
    if (!sk) { ESP_LOGE("MyNVS", "键名为空"); return ESP_ERR_INVALID_ARG; }

    Guard guard(nvs_, /*need_write=*/false);
    if (!guard.ok) { ESP_LOGE("MyNVS", "NVS 未打开或实例已失效"); return ESP_ERR_INVALID_STATE; }

    size_t len = value.size();   // 期望长度
    return nvs_get_blob(nvs_->handle, sk.c_str(), value.data(), &len);
}

/// 写入：两种容器行为完全一致，合并为一个约束
template <typename C>
    requires ResizableByteBuffer<C> || FixedByteBuffer<C>
esp_err_t MyNVS::write(std::string_view key, const C& value)
{
    SafeKey sk(key);
    if (!sk) { ESP_LOGE("MyNVS", "键名为空"); return ESP_ERR_INVALID_ARG; }

    Guard guard(nvs_, /*need_write=*/true);
    if (!guard.ok) { ESP_LOGE("MyNVS", "NVS 未打开或只读"); return ESP_ERR_INVALID_STATE; }

    return nvs_set_blob(nvs_->handle, sk.c_str(), value.data(), value.size());
}

#endif /* _MY_NVS_H_ */