/**
 * @file    test_nvs.cc
 * @brief   MyNVS 组件的单元测试（Unity 框架）。
 *
 * 覆盖范围：
 *   - 字符串 / 原始 blob / 字节容器（vector / array）读写
 *   - 标量：bool / char / enum / 整数 / 浮点
 *   - find / erase_key / erase_all / commit
 *   - 引用计数：多实例共享槽位
 *   - 只读模式不可写
 *   - 边界：空 key、固定容器长度不匹配、持久化
 *
 * 注意：NVS 命名空间最多 15 字符；每个测试使用独立命名空间，
 *       且 MyNVS 析构会 ref--，测试结束时应回到零引用。
 */

#include <unity.h>
#include "my_nvs.h"
#include <nvs_flash.h>
#include <vector>
#include <array>
#include <cstring>
// ------------------------------------------------------------------
// 公用
// ------------------------------------------------------------------

// 测试使用的命名空间：≤ 15 字符
static constexpr const char* kNS      = "test_nvs";
static constexpr const char* kNS_ALT  = "test_alt";    // 用于引用计数
static constexpr const char* kNS_RO   = "test_ro";     // 用于只读测试
static constexpr const char* kNS_PERS = "test_pers";   // 用于持久化测试

enum class Color : uint8_t { Red = 1, Green = 2, Blue = 3 };

// 清空并打开一个读写命名空间
static void reset_rw(const char* ns) {
    MyNVS nvs(ns, NVS_READWRITE);
    nvs.erase_all();
    nvs.commit();
}

// ------------------------------------------------------------------
// 字符串
// ------------------------------------------------------------------

TEST_CASE("string: write / read / delete", "[my_nvs][string]")
{
    MyNVS nvs(kNS, NVS_READWRITE);
    nvs.erase_all();

    // 写 + 读
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("name", std::string("ESP32")));
    std::string v;
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("name", v));
    TEST_ASSERT_EQUAL_STRING("ESP32", v.c_str());

    // 空字符串
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("empty", std::string("")));
    v = "not-empty";
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("empty", v));
    TEST_ASSERT_TRUE(v.empty());

    // 覆盖
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("name", std::string("ESP32-S3")));
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("name", v));
    TEST_ASSERT_EQUAL_STRING("ESP32-S3", v.c_str());

    // 未找到(不修改原值)
    v = "ESP32";
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, nvs.read("missing", v));
    TEST_ASSERT_EQUAL_STRING("ESP32", v.c_str());

    // 删除
    TEST_ASSERT_EQUAL(ESP_OK, nvs.erase_key("name"));
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, nvs.read("name", v));
}

// ------------------------------------------------------------------
// 标量
// ------------------------------------------------------------------

TEST_CASE("scalar: bool / char / integer widths", "[my_nvs][scalar]")
{
    MyNVS nvs(kNS, NVS_READWRITE);
    nvs.erase_all();

    // bool
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("b", true));
    bool b = false;
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("b", b));
    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("b", false));
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("b", b));
    TEST_ASSERT_FALSE(b);

    // char
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("c", char{'Z'}));
    char c = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("c", c));
    TEST_ASSERT_EQUAL('Z', c);

    // enum（必须 : uint8_t）
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("e", Color::Blue));
    Color e = Color::Red;
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("e", e));
    TEST_ASSERT_EQUAL(static_cast<int>(Color::Blue), static_cast<int>(e));

    // 整数各宽度
    int8_t   i8  = -128;  TEST_ASSERT_EQUAL(ESP_OK, nvs.write("i8",  i8));
    int16_t  i16 = -12345; TEST_ASSERT_EQUAL(ESP_OK, nvs.write("i16", i16));
    int32_t  i32 = -123456789; TEST_ASSERT_EQUAL(ESP_OK, nvs.write("i32", i32));
    int64_t  i64 = INT64_C(-1234567890123456789); TEST_ASSERT_EQUAL(ESP_OK, nvs.write("i64", i64));

    int8_t   i8r  = 0;  TEST_ASSERT_EQUAL(ESP_OK, nvs.read("i8",  i8r));
    int16_t  i16r = 0;  TEST_ASSERT_EQUAL(ESP_OK, nvs.read("i16", i16r));
    int32_t  i32r = 0;  TEST_ASSERT_EQUAL(ESP_OK, nvs.read("i32", i32r));
    int64_t  i64r = 0;  TEST_ASSERT_EQUAL(ESP_OK, nvs.read("i64", i64r));
    int64_t  bad = i64r + 1;
    TEST_ASSERT_EQUAL(i8,  i8r);
    TEST_ASSERT_EQUAL(i16, i16r);
    TEST_ASSERT_EQUAL(i32, i32r);
    TEST_ASSERT_EQUAL_INT64(i64, i64r);
    TEST_ASSERT_NOT_EQUAL(bad, i64r);

    uint8_t  u8  = 255;          TEST_ASSERT_EQUAL(ESP_OK, nvs.write("u8",  u8));
    uint16_t u16 = 65535;        TEST_ASSERT_EQUAL(ESP_OK, nvs.write("u16", u16));
    uint32_t u32 = 0xDEADBEEF;   TEST_ASSERT_EQUAL(ESP_OK, nvs.write("u32", u32));
    uint64_t u64 = 0xCAFEBABE12345678ULL; TEST_ASSERT_EQUAL(ESP_OK, nvs.write("u64", u64));

    uint8_t  u8r  = 0;  TEST_ASSERT_EQUAL(ESP_OK, nvs.read("u8",  u8r));
    uint16_t u16r = 0;  TEST_ASSERT_EQUAL(ESP_OK, nvs.read("u16", u16r));
    uint32_t u32r = 0;  TEST_ASSERT_EQUAL(ESP_OK, nvs.read("u32", u32r));
    uint64_t u64r = 0;  TEST_ASSERT_EQUAL(ESP_OK, nvs.read("u64", u64r));
    TEST_ASSERT_EQUAL_UINT32(u8,  u8r);
    TEST_ASSERT_EQUAL_UINT32(u16, u16r);
    TEST_ASSERT_EQUAL_HEX32(u32, u32r);
    TEST_ASSERT_EQUAL_HEX64(u64, u64r);
}

TEST_CASE("scalar: float / double bit-mode", "[my_nvs][scalar]")
{
    MyNVS nvs(kNS, NVS_READWRITE);
    nvs.erase_all();

    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("f", 1.5f));
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("d", 3.14159265358979));

    float  f = 0.0f;
    double d = 0.0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("f", f));
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("d", d));

    TEST_ASSERT_EQUAL_FLOAT(1.5f, f);
    TEST_ASSERT_EQUAL_DOUBLE(3.14159265358979, d);
}

// ------------------------------------------------------------------
// 原始 blob
// ------------------------------------------------------------------

TEST_CASE("blob: raw void* read / write", "[my_nvs][blob]")
{
    MyNVS nvs(kNS, NVS_READWRITE);
    nvs.erase_all();

    const uint8_t src[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("blob", src, sizeof(src)));

    // 只查长度
    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("blob", (void*)nullptr, &len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(src), len);

    // 真正读
    uint8_t dst[sizeof(src)] = {0};
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("blob", dst, &len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(src), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, sizeof(src));

    // 空 blob
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("empty", (const void*)nullptr, 0));
    len = 123;
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("empty", (void*)nullptr, &len));
    TEST_ASSERT_EQUAL_UINT32(0, len);
}

// ------------------------------------------------------------------
// 字节容器
// ------------------------------------------------------------------

TEST_CASE("byte container: vector (resizable)", "[my_nvs][container]")
{
    MyNVS nvs(kNS, NVS_READWRITE);
    nvs.erase_all();

    std::vector<uint8_t> v = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("v", v));

    std::vector<uint8_t> r;
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("v", r));
    TEST_ASSERT_EQUAL_UINT32(v.size(), r.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v.data(), r.data(), v.size());

    // 覆盖一个更长的 vector
    std::vector<uint8_t> longv(300, 0xAB);
    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("v", longv));
    std::vector<uint8_t> longr;
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("v", longr));
    TEST_ASSERT_EQUAL_UINT32(300, longr.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(longv.data(), longr.data(), longv.size());
}

TEST_CASE("byte container: array (fixed size)", "[my_nvs][container]")
{
    MyNVS nvs(kNS, NVS_READWRITE);
    nvs.erase_all();

    std::array<uint8_t, 8> a;
    for (size_t i = 0; i < a.size(); ++i) a[i] = static_cast<uint8_t>(i * 3);

    TEST_ASSERT_EQUAL(ESP_OK, nvs.write("arr", a));

    std::array<uint8_t, 8> r{};
    TEST_ASSERT_EQUAL(ESP_OK, nvs.read("arr", r));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a.data(), r.data(), a.size());

    // 固定容器长度不匹配 → ESP_ERR_NVS_INVALID_LENGTH
    std::array<uint8_t, 4> wrong{};
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_INVALID_LENGTH, nvs.read("arr", wrong));
}

// ------------------------------------------------------------------
// find / erase_all / commit
// ------------------------------------------------------------------

TEST_CASE("find / erase_all / commit", "[my_nvs][misc]")
{
    MyNVS nvs(kNS, NVS_READWRITE);
    nvs.erase_all();

    // find 未找到
    nvs_type_t t = NVS_TYPE_ANY;
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, nvs.find("nope", t));

    // 分别写入多种类型，核对 find 的类型
    nvs.write("s",  std::string("x"));
    nvs.write("u8", uint8_t{1});
    nvs.write("i32",int32_t{1});
    nvs.write("v",  std::vector<uint8_t>{1, 2});

    TEST_ASSERT_EQUAL(ESP_OK, nvs.find("s",   t)); TEST_ASSERT_EQUAL(NVS_TYPE_STR, t);
    TEST_ASSERT_EQUAL(ESP_OK, nvs.find("u8",  t)); TEST_ASSERT_EQUAL(NVS_TYPE_U8,  t);
    TEST_ASSERT_EQUAL(ESP_OK, nvs.find("i32", t)); TEST_ASSERT_EQUAL(NVS_TYPE_I32, t);
    TEST_ASSERT_EQUAL(ESP_OK, nvs.find("v",   t)); TEST_ASSERT_EQUAL(NVS_TYPE_BLOB,t);

    // commit 后可正常读取
    TEST_ASSERT_EQUAL(ESP_OK, nvs.commit());

    // erase_all 之后再 find 应全部失效
    TEST_ASSERT_EQUAL(ESP_OK, nvs.erase_all());
    TEST_ASSERT_EQUAL(ESP_OK, nvs.commit());

    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, nvs.find("s", t));
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, nvs.find("v", t));
}

// ------------------------------------------------------------------
// 引用计数：两个实例共享同一 nvs_handle_t
// ------------------------------------------------------------------

TEST_CASE("reference count: shared handle across instances", "[my_nvs][refcount]")
{
    // 先清空
    reset_rw(kNS_ALT);

    {
        MyNVS a(kNS_ALT, NVS_READWRITE);

        // a 写入
        TEST_ASSERT_EQUAL(ESP_OK, a.write("k", std::string("v1")));

        {
            MyNVS b(kNS_ALT, NVS_READWRITE);   // 复用，ref = 2
            std::string v;
            TEST_ASSERT_EQUAL(ESP_OK, b.read("k", v));
            TEST_ASSERT_EQUAL_STRING("v1", v.c_str());
        } // b 析构：ref 2 -> 1，handle 不关

        // a 仍可继续读，且还能读到 b 之前写的（同一 handle）
        std::string v;
        TEST_ASSERT_EQUAL(ESP_OK, a.read("k", v));
        TEST_ASSERT_EQUAL_STRING("v1", v.c_str());

        TEST_ASSERT_EQUAL(ESP_OK, a.write("k2", std::string("v2")));
    } // a 析构：ref 1 -> 0，commit + close

    // 重新以只读方式打开，验证数据已持久化
    {
        MyNVS c(kNS_ALT, NVS_READONLY);
        std::string v;
        TEST_ASSERT_EQUAL(ESP_OK, c.read("k",  v)); TEST_ASSERT_EQUAL_STRING("v1", v.c_str());
        TEST_ASSERT_EQUAL(ESP_OK, c.read("k2", v)); TEST_ASSERT_EQUAL_STRING("v2", v.c_str());
    }
}

// ------------------------------------------------------------------
// 只读模式：不可写
// ------------------------------------------------------------------

TEST_CASE("readonly mode: write rejected", "[my_nvs][readonly]")
{
    esp_log_level_set("*", ESP_LOG_NONE);
    // 先清空并用读写写一条数据
    {
        MyNVS nvs(kNS_RO, NVS_READWRITE);
        nvs.erase_all();
        TEST_ASSERT_EQUAL(ESP_OK, nvs.write("k", std::string("hello")));
        TEST_ASSERT_EQUAL(ESP_OK, nvs.commit());
    }

    // 只读打开：可读不可写
    MyNVS ro(kNS_RO, NVS_READONLY);

    std::string v;
    TEST_ASSERT_EQUAL(ESP_OK, ro.read("k", v));
    TEST_ASSERT_EQUAL_STRING("hello", v.c_str());

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ro.write("k", std::string("world")));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ro.erase_key("k"));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ro.erase_all());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ro.commit());
    esp_log_level_set("*", ESP_LOG_INFO);
}

// ------------------------------------------------------------------
// 边界：空 key
// ------------------------------------------------------------------

TEST_CASE("edge: empty key rejected", "[my_nvs][edge]")
{
    esp_log_level_set("*", ESP_LOG_NONE);
    MyNVS nvs(kNS, NVS_READWRITE);

    std::string v = "x";
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, nvs.read("", v));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, nvs.write("", std::string("v")));

    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, nvs.read("", (void*)nullptr, &len));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, nvs.write("", (const void*)nullptr, 0));
    esp_log_level_set("*", ESP_LOG_INFO);
}

// ------------------------------------------------------------------
// 持久化：重新打开后仍能读到
// ------------------------------------------------------------------

TEST_CASE("persistence: reopen sees committed data", "[my_nvs][persist]")
{
    // 写
    {
        MyNVS nvs(kNS_PERS, NVS_READWRITE);
        nvs.erase_all();
        TEST_ASSERT_EQUAL(ESP_OK, nvs.write("str", std::string("persist")));
        TEST_ASSERT_EQUAL(ESP_OK, nvs.write("num", int32_t{0x12345678}));
        std::vector<uint8_t> v{9, 8, 7};
        TEST_ASSERT_EQUAL(ESP_OK, nvs.write("vec", v));
        TEST_ASSERT_EQUAL(ESP_OK, nvs.commit());
    }

    // 读
    {
        MyNVS nvs(kNS_PERS, NVS_READONLY);

        std::string s;
        TEST_ASSERT_EQUAL(ESP_OK, nvs.read("str", s));
        TEST_ASSERT_EQUAL_STRING("persist", s.c_str());

        int32_t n = 0;
        TEST_ASSERT_EQUAL(ESP_OK, nvs.read("num", n));
        TEST_ASSERT_EQUAL_HEX32(0x12345678, n);

        std::vector<uint8_t> v;
        TEST_ASSERT_EQUAL(ESP_OK, nvs.read("vec", v));
        TEST_ASSERT_EQUAL_UINT32(3, v.size());
        TEST_ASSERT_EQUAL_UINT8(9, v[0]);
        TEST_ASSERT_EQUAL_UINT8(7, v[2]);
    }
}

// ------------------------------------------------------------------
// 入口
// ------------------------------------------------------------------
static void print_banner(const char* msg)
{
    printf("\n=================== %s ===================\n", msg);
}
extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    print_banner("Run all tests");
    unity_run_all_tests();
    // 不要读 Unity 字段，不要打印
}

