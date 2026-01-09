/*
 *             Copyright [2025] [samllin]
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
#ifndef MY_NVS_HPP_
#define MY_NVS_HPP_

#include <string>
#include <cstring>
#include <mutex>
#include <cstdint>
#include <concepts>
#include <type_traits>
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "my_nvs_manager.hpp"


#define PARTITION_LENGTH    15
#define NAMESPACE_LENGTH    15
#define KEY_LENGTH          15


// 定义概念
template<typename T>
concept BoolType = std::is_same_v<T, bool>;
template<typename T>
concept EnumType = std::is_enum_v<T>;
template<typename T>
concept CharType = std::is_same_v<T, char>;
template<typename T>
concept IntegerType = std::is_integral_v<T> && !std::is_same_v<T, bool>;
template<typename T>
concept FloatingType = std::is_floating_point_v<T>;
template<typename T>
concept SupportedType = BoolType<T> || EnumType<T> || CharType<T> || IntegerType<T> || FloatingType<T>;

// 辅助模板：用于 static_assert 报错
template<class> inline constexpr bool always_false = false;

struct my_nvs_t;
class MyNVS_Manager;
class MyNVS {
public:
    explicit MyNVS(const char* name_space, nvs_open_mode_t mode = NVS_READONLY);
    MyNVS(const char* partition, const char* name_space, nvs_open_mode_t mode = NVS_READONLY);
    ~MyNVS();
    

    // 字符串数据读取
    esp_err_t read(const char* key, char* value);
    esp_err_t read(const std::string& key, char* value) {
        return read(key.c_str(), value);
    }
    esp_err_t read(const char* key, std::string& value);
    esp_err_t read(const std::string& key, std::string& value)
    {
        return read(key.c_str(), value.c_str());
    }
    // 二进制数据读取
    esp_err_t read(const char* key, void* value, size_t* length);
    esp_err_t read(const std::string& key, void* value, size_t* length)
    {
        return read(key.c_str(), value, length);
    }

    // 字符串和二进制数据写入
    esp_err_t write(const char* key, const char* value);
    esp_err_t write(const std::string& key, const char* value)
    {
        return write(key.c_str(), value);
    }
    esp_err_t write(const char* key, const std::string& value)
    {
        return write(key, value.c_str());
    }
    esp_err_t write(const std::string& key, const std::string& value)
    {
        return write(key.c_str(), value.c_str());
    }
    esp_err_t write(const char* key, const void* value, size_t length);
    esp_err_t write(const std::string& key, const void* value, size_t length)
    {
        return write(key.c_str(), value, length);
    }

    // 其他操作
    esp_err_t find(const char* key);
    esp_err_t find(const std::string& key);
    esp_err_t find(const char* key, nvs_type_t* out_type);
    esp_err_t find(const std::string& key, nvs_type_t* out_type);
    esp_err_t erase_key(const char* key);
    esp_err_t erase_key(const std::string& key);
    esp_err_t erase_all();
    esp_err_t commit();

    // 读取函数模板（引用版本）
    template <SupportedType T>
    esp_err_t read(const char* key, T& value);

    // 读取重载
    template <SupportedType T>
    esp_err_t read(const char* key, T* value);
    template <SupportedType T>
    esp_err_t read(const std::string& key, T* value);
    template <SupportedType T>
    esp_err_t read(const std::string& key, T& value);

    // 写入函数模板
    template <SupportedType T>
    esp_err_t write(const char* key, const T& value);
    template <SupportedType T>
    esp_err_t write(const std::string& key, const T& value);
      

private:
    inline bool is_valid() const {
        return m_nvs && m_nvs->handle != 0;
    }
    my_nvs_t*       m_nvs;
    MyNVS_Manager*  m_manager;
};



// ======================================================
// 模板函数实现
// ======================================================

// 模板读取实现
template <SupportedType T>
esp_err_t MyNVS::read(const char* key, T& value)
{
    if (key == nullptr || *key == '\0') {
        ESP_LOGE("MyNVS-HPP", "键名为空");
        return ESP_ERR_INVALID_ARG;
    }

    char safe_key[KEY_LENGTH + 1];
    if (strlen(key) > KEY_LENGTH) {
        strncpy(safe_key, key, KEY_LENGTH);
        safe_key[KEY_LENGTH] = '\0';
        ESP_LOGW("MyNVS-HPP", "key length is too loog, original key=%s key=%s, may be cause error!", key, safe_key);
        key = safe_key;
    }
    
    if(!m_nvs) {
        ESP_LOGE("MyNVS-HPP", "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    std::unique_lock<std::mutex> lock(m_nvs->mutex, std::try_to_lock);
    if(!lock.owns_lock() || m_nvs->handle == 0) {
        ESP_LOGE("MyNVS-HPP", "尝试加锁失败或NVS已关闭");
        return ESP_FAIL;
    }
    
    if constexpr(BoolType<T>) {
        uint8_t tmp = 0;
        auto err = nvs_get_u8(m_nvs->handle, key, &tmp);
        if(ESP_OK == err) {
            value = (tmp != 0);
            return ESP_OK;
        }
        return err;
    } else if constexpr(EnumType<T>) {
        uint8_t tmp = static_cast<uint8_t>(value);
        auto err = nvs_get_u8(m_nvs->handle, key, (uint8_t*)(&value));
        return err;
    } else if constexpr(CharType<T>) {
        uint8_t tmp = 0;
        auto err = nvs_get_u8(m_nvs->handle, key, &tmp);
        if(ESP_OK == err) {
            value = static_cast<char>(tmp);
            return ESP_OK;
        }
        return err;
    } else if constexpr(IntegerType<T>) {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 ||sizeof(T) == 4 ||sizeof(T) == 8, "不支持的整数大小，当前仅支持1/2/4/8字节整数");
        esp_err_t err = ESP_ERR_NOT_SUPPORTED;
        if constexpr(sizeof(T) == 1) {
            if constexpr(std::is_signed_v<T>) {
                int8_t tmp = 0;
                err = nvs_get_i8(m_nvs->handle, key, &tmp);
                if(ESP_OK == err) {
                    value = tmp;
                }
            } else {
                uint8_t tmp = 0;
                err = nvs_get_u8(m_nvs->handle, key, &tmp);
                if(ESP_OK == err) {
                    value = tmp;
                }
            }
        } else if constexpr(sizeof(T) == 2) {
            if constexpr(std::is_signed_v<T>) {
                int16_t tmp = 0;
                err = nvs_get_i16(m_nvs->handle, key, &tmp);
                if(ESP_OK == err) {
                    value = tmp;
                }
            } else {
                uint16_t tmp;
                err = nvs_get_u16(m_nvs->handle, key, &tmp);
                if(ESP_OK == err) {
                    value = tmp;
                }
            }
        } else if constexpr(sizeof(T) == 4) {
            if constexpr(std::is_signed_v<T>) {
                int32_t tmp = 0;
                err = nvs_get_i32(m_nvs->handle, key, &tmp);
                if(ESP_OK == err) {
                    value = tmp;
                }
            } else {
                uint32_t tmp = 0;
                err = nvs_get_u32(m_nvs->handle, key, &tmp);
                if(ESP_OK == err) {
                    value = tmp;
                }
            }
        } else if constexpr(sizeof(T) == 8) {
            if constexpr(std::is_signed_v<T>) {
                int64_t tmp = 0;
                err = nvs_get_i64(m_nvs->handle, key, &tmp);
                if(ESP_OK == err) {
                    value = tmp;
                }
            } else {
                uint64_t tmp = 0;
                err = nvs_get_u64(m_nvs->handle, key, &tmp);
                if(ESP_OK == err) {
                    value = tmp;
                }
            }
        }
        return err;
    } else if constexpr(FloatingType<T>) {
        using StorageType = std::conditional_t<sizeof(T) <= 4, uint32_t, uint64_t>;
        static_assert(sizeof(T) == sizeof(StorageType), "浮点类型大小不匹配");
        StorageType tmp = 0;
        esp_err_t err = ESP_OK;
        if constexpr(sizeof(T) <= 4) {
            err = nvs_get_u32(m_nvs->handle, key, &tmp);
        } else {
            err = nvs_get_u64(m_nvs->handle, key, &tmp);
        }
        if(ESP_OK == err) {
            std::memcpy(&value, &tmp, sizeof(T));
            return ESP_OK;
        }
        return err;
    } else {
        static_assert(always_false<T>, "暂不支持该类型");
        return ESP_ERR_NOT_SUPPORTED;
    }
}
// 模板写入实现
template <SupportedType T>
esp_err_t MyNVS::write(const char* key, const T& value)
{
    if (key == nullptr || *key == '\0') {
        ESP_LOGE("MyNVS-HPP", "键名为空");
        return ESP_ERR_INVALID_ARG;
    }
    char safe_key[KEY_LENGTH + 1];
    if (strlen(key) > KEY_LENGTH) {
        strncpy(safe_key, key, KEY_LENGTH);
        safe_key[KEY_LENGTH] = '\0';
        ESP_LOGW("MyNVS-HPP", "key length is too loog, original key=%s key=%s, may be cause error!", key, safe_key);
        key = safe_key;
    }

    if(!m_nvs || m_nvs->open_mode != NVS_READWRITE) {
        ESP_LOGE("MyNVS-HPP", "NVS只读或实例已失效");
        return ESP_FAIL;
    }
    std::unique_lock<std::mutex> lock(m_nvs->mutex, std::try_to_lock);
    if(!lock.owns_lock() || !is_valid()) {
        ESP_LOGE("MyNVS-HPP", "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    if constexpr(BoolType<T>) {
        return nvs_set_u8(m_nvs->handle, key, value ? 1 : 0);
    } else if constexpr(EnumType<T>) {
        return nvs_set_u8(m_nvs->handle, key, static_cast<uint8_t>(value));
    } else if constexpr(CharType<T>) {
        return nvs_set_u8(m_nvs->handle, key, static_cast<uint8_t>(value));
    } else if constexpr(IntegerType<T>) {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 ||sizeof(T) == 4 ||sizeof(T) == 8, "不支持的整数大小，当前仅支持1/2/4/8字节整数");
        esp_err_t err = ESP_ERR_NOT_SUPPORTED;
        if constexpr(sizeof(T) == 1) {
            if constexpr(std::is_signed_v<T>) {
                return nvs_set_i8(m_nvs->handle, key, value);
            } else {
                return nvs_set_u8(m_nvs->handle, key, value);
            }
        } else if constexpr(sizeof(T) == 2) {
            if constexpr(std::is_signed_v<T>) {
                return nvs_set_i16(m_nvs->handle, key, value);
            } else {
                return nvs_set_u16(m_nvs->handle, key, value);
            }
        } else if constexpr(sizeof(T) == 4) {
            if constexpr(std::is_signed_v<T>) {
                return nvs_set_i32(m_nvs->handle, key, value);
            } else {
                return nvs_set_u32(m_nvs->handle, key, value);
            }
        } else if constexpr(sizeof(T) == 8) {
            if constexpr(std::is_signed_v<T>) {
                return nvs_set_i64(m_nvs->handle, key, value);
            } else {
                return nvs_set_u64(m_nvs->handle, key, value);
            }
        }
        ESP_LOGE("MyNVS-HPP", "写入%s失败: %s", key, esp_err_to_name(err));
        return err;
    } else if constexpr(FloatingType<T>) {
        using StorageType = std::conditional_t<sizeof(T) <= 4, uint32_t, uint64_t>;
        static_assert(sizeof(T) == sizeof(StorageType), "浮点类型大小不匹配");
        StorageType tmp;
        std::memcpy(&tmp, &value, sizeof(T));
        if constexpr(sizeof(T) <= 4) {
            return nvs_set_u32(m_nvs->handle, key, tmp);
        } else {
            return nvs_set_u64(m_nvs->handle, key, tmp);
        }
    } else {
        static_assert(always_false<T>, "暂不支持该类型");
        return ESP_ERR_NOT_SUPPORTED;
    }
}

// 读取重载
template <SupportedType T>
esp_err_t MyNVS::read(const char* key, T* value)
{
    return read(key, *value);
}
template <SupportedType T>
esp_err_t MyNVS::read(const std::string& key, T* value)
{
    return read(key.c_str(), *value);
}
template <SupportedType T>
esp_err_t MyNVS::read(const std::string& key, T& value)
{
    return read(key.c_str(), value);
}

// 写入重载
template <SupportedType T>
esp_err_t MyNVS::write(const std::string& key, const T& value) 
{
    return write(key.c_str(), value);
}

#endif