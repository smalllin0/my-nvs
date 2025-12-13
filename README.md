# MyNVS 组件

- MyNVS组件是对ESP-IDF 的 NVS API的封装，提供更易用接口来操作NVS。

## To do


## 目录

- [主要特性](#主要特性)
- [API参考](#API参考)
- [使用例程](#使用例程)
- [注意事件](#注意事项)
- [配置选项](#配置选项)
- [依赖](#依赖)
- [许可](#许可)

## 主要特性

- **支持更多数据类型**
    1. 原生数据：```int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t、字符串、blob```
    2. 扩展支持：```bool int enum(8bits has been tested) long float double std::string ```
- **统一的操作API**
    1. 读写操作统一使用read/write方法完成
- **线程操作安全**
    1. 通过线程锁std::lock保证单线程安全
- **自动提交**
    1. 在关闭命名空间时，自动提交更改
    2. 提供手动提交方法
- **错误处理**
    1. 所有方法返回原生API相同的错误代码，方便处理故障

## API参考
- 读写类
```
esp_err_t read(TYPE1 key, TYPE2 value);
esp_err_t read(TYPE2 key, void* value, size_t* length);
esp_err_t write(TYPE1 key, TYPE3 value);
esp_err_t write(TYPE2 key, const void* value, size_t* length);


/*
 * TYPE1：const char*、const std::string&
 * TYPE2：支持更多数据类型中数据类型的引用、指针版本
 * TYPE3：支持更多数据类型中数据类型的引用
 */

```
- 查找
```
esp_err_t find(const char* key);
esp_err_t find(const std::string& key);
esp_err_t find(const char* key, nvs_type_t* out_type);
esp_err_t find(const std::string& key, nvs_type_t* out_type);
```
- 删除提交
```
esp_err_t erase_key(const char* key);
esp_err_t erase_key(const std::string& key);
esp_err_t erase_all();
esp_err_t commit();
```

## 使用例程

```cpp
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "my_nvs.hpp"
#include "my_storage.h"
#include "cstddef"

#define TAG "main"


extern "C" void app_main(void)
{
    // data to store
    bool flag = true;
    char ch = 'a';
    int a = 32;
    long b = 43L;
    float c = 32.1;
    double d = 22.001;
    // 注意，浮点数本身是存在精度问题的
    ESP_LOGW(TAG, "float c=%f, double d=%lf", c, d);

    // open a default partition NVS: "nvs"，namespace too long will use substr: test12345678901, when namespace  is 
    // 打开一个默认分区（nvs），名字空间为test12345678901（长度超长），模式为读写模式
    // 1. 将使用截断的名字空间：test12345678901
    // 2. 注意有这可能造成写入错误的名字空间中
    MyNVS nvs("test1234567890123", NVS_READWRITE);
    nvs.write("flag", flag);
    nvs.write("ch", ch);
    nvs.write("a", a);
    nvs.write("b", b);
    nvs.write("c", c);
    nvs.write("d", d);

    bool f = false;
    char ch1 = 'z';
    int a1 = 10;
    long b1 = 7;
    float c1 = 0.0;
    double d1 = 0.0;
    nvs.read("flag", f);
    nvs.read("ch", ch1);
    nvs.read("a", a1);
    nvs.read("b", b1);
    nvs.read("c", c1);
    nvs.read("d", d1);

    ESP_LOGE(TAG, "flag=%d,ch=%c,a=%d,b=%ld,c1=%f,d1=%lf,c=%f,d=%lf",f,ch1,a1,b1,c1,d1,c,d);

    // 写入一个键超长的键值对时同理（会自动截断，可能导致键名重复）
    nvs.write("h1234567890123456", 12);

    // 操作不存在的键时，
    // 1. 将提示出错```E (284) MyNVS: 读取nihao失败: ESP_ERR_NVS_NOT_FOUND```
    // 2. 不修改其原来值
    // 3. 返回原api的错误码
    std::string str = "hello";
    auto err = nvs.read("nihao", str);
    ESP_LOGI(TAG, "str = %s, err=%s", str.c_str(), esp_err_to_name(err));

    // 支持引用、指针的第二参数（为防止代码膨胀，请使用一种风格）
    int style1 = 0;
    int style2 = 0;
    nvs.read("a", style1);
    nvs.read("a", &style2);
    ESP_LOGI(TAG, "style1=%d, style2=%d", style1, style2);
}

```


## 注意事项
1. 默认启动自动初始化，不应当再进行```nvs_flash_init()```初始化操作；
2. 浮点类型本身存在精度问题，使用时请小心；

## 配置选项
- 在menuconfig中配置最大命名空间数量及特性
```
Component config -> MyNVS 组件配置 -> 
    (4) 操作名字空间数量
    [ ] 初始化NVS时，遇到没有空闲页面自动进行擦除
    [*] 初始化NVS时，发现新版本格式自动进行擦除
```
## 依赖
- ESP-IDF 5.4+（其他版本未测试）
- C++ 20标准

## 许可
- Licenses: Aapche 2.0 
- Copyright: samllin

