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

#include <vector>
#include "my_nvs.hpp"

#define TAG "MyNVS"

MyNVS::MyNVS(const char* name_space, nvs_open_mode_t mode)
{
    m_manager = MyNVS_Manager::get_instance();
    char safe_namespace[NAMESPACE_LENGTH + 1];
    if (strlen(name_space) > NAMESPACE_LENGTH) {
        strncpy(safe_namespace, name_space, NAMESPACE_LENGTH);
        safe_namespace[NAMESPACE_LENGTH] = '\0';
        ESP_LOGW(TAG, "namespace name is too loog, original namespace=%s, use namespace=%s now, may be cause error!", name_space, safe_namespace);
        name_space = safe_namespace;
    }
    auto index = m_manager->open("nvs", name_space, mode);
    if (index == INVALID_INDEX) {
        m_nvs = nullptr;
        ESP_LOGE(TAG, "打开分区失败");
        return;
    }
        
    m_nvs = m_manager->get_nvs(index);
}

MyNVS::MyNVS(const char* partition, const char* name_space, nvs_open_mode_t mode)
{
    m_manager = MyNVS_Manager::get_instance();
    char safe_namespace[NAMESPACE_LENGTH + 1];
    if (strlen(name_space) > NAMESPACE_LENGTH) {
        strncpy(safe_namespace, name_space, NAMESPACE_LENGTH);
        safe_namespace[NAMESPACE_LENGTH] = '\0';
        ESP_LOGW(TAG, "namespace name is too loog, original namespace=%s, use namespace=%s now, may be cause error!", name_space, safe_namespace);
        name_space = safe_namespace;
    }
    char safe_partition_name[PARTITION_LENGTH + 1];
    if (strlen(partition) > PARTITION_LENGTH) {
        strncpy(safe_partition_name, partition, PARTITION_LENGTH);
        safe_partition_name[PARTITION_LENGTH] = '\0';
        ESP_LOGW(TAG, "namespace name length is too loog, use namespace=%s, may be cause error!", partition);
        ESP_LOGW(TAG, "partition name is too loog, original partition=%s, use partition=%s now, may be cause error!", partition, safe_partition_name);
        partition = safe_partition_name;
    }


    auto index = m_manager->open(partition, name_space, mode);
    if (index != INVALID_INDEX) {
        m_nvs = m_manager->get_nvs(index);
    } else {
        m_nvs = nullptr;
        ESP_LOGE(TAG, "打开分区失败");
    }
}

MyNVS::~MyNVS()
{
    if (m_manager && m_nvs) {
        m_manager->close(m_nvs);
        m_nvs = nullptr;
    }
    m_manager = nullptr;
}

// =============================================
// 非模板成员函数实现
// =============================================

// --- 字符串读取 ---
esp_err_t MyNVS::read(const char* key, char* value)
{
    if (key == nullptr || *key == '\0' || value == nullptr) {
        ESP_LOGE(TAG, "键名为空/保存地址为空");
        return ESP_ERR_INVALID_ARG;
    }
    if(!m_nvs) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }

    std::unique_lock<std::mutex> lock(m_nvs->mutex, std::try_to_lock);
    if(!lock.owns_lock() || !is_valid()) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    size_t len = 0;
    auto err = nvs_get_str(m_nvs->handle, key, nullptr, &len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读取%s失败: %s", key, esp_err_to_name(err));
        return err;
    } else {
        err = nvs_get_str(m_nvs->handle, key, value, &len);
        if (err == ESP_OK) {
            return ESP_OK;
        }
    }
    
    ESP_LOGE(TAG, "读取%s失败: %s", key, esp_err_to_name(err));
    return err;
}

esp_err_t MyNVS::read(const char* key, std::string &value)
{
    if (key == nullptr || *key == '\0') {
        ESP_LOGE(TAG, "键名为空");
        return ESP_ERR_INVALID_ARG;
    }
    char safe_key[KEY_LENGTH + 1];
    if (strlen(key) > KEY_LENGTH) {
        strncpy(safe_key, key, KEY_LENGTH);
        safe_key[KEY_LENGTH] = '\0';
        ESP_LOGW(TAG, "key length is too loog, original key=%s key=%s, may be cause error!", key, safe_key);
        key = safe_key;
    }
    
    if(!m_nvs) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    std::unique_lock<std::mutex> lock(m_nvs->mutex, std::try_to_lock);
    if(!lock.owns_lock() || !is_valid()) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }

    size_t len = 0;
    auto err = nvs_get_str(m_nvs->handle, key, nullptr, &len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读取%s失败: %s", key, esp_err_to_name(err));
        return err;
    } else {
        if(len == 0) {
            value.clear();
            return ESP_OK;
        }
    }

    value.resize(len - 1);
    err = nvs_get_str(m_nvs->handle, key, value.data(), &len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读取%s失败: %s", key, esp_err_to_name(err));
        value.clear();
    } else {
        value.resize(len - 1);
    }

    return err;
}

// --- Blob读取 ---
esp_err_t MyNVS::read(const char* key, void* value, size_t* length)
{
    if (key == nullptr || *key == '\0') {
        ESP_LOGE(TAG, "键名为空");
        return ESP_ERR_INVALID_ARG;
    }
    char safe_key[KEY_LENGTH + 1];
    if (strlen(key) > KEY_LENGTH) {
        strncpy(safe_key, key, KEY_LENGTH);
        safe_key[KEY_LENGTH] = '\0';
        ESP_LOGW(TAG, "key length is too loog, original key=%s key=%s, may be cause error!", key, safe_key);
        key = safe_key;
    }
    
    if(!m_nvs) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    std::unique_lock<std::mutex> lock(m_nvs->mutex, std::try_to_lock);
    if(!lock.owns_lock() || !is_valid()) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    auto err = nvs_get_blob(m_nvs->handle, key, value, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读取%s失败: %s", key, esp_err_to_name(err));
    }
    return err;
}

// --- 字符串写入 ---
esp_err_t MyNVS::write(const char* key, const char* value)
{
    if (key == nullptr || *key == '\0') {
        ESP_LOGE(TAG, "键名为空");
        return ESP_ERR_INVALID_ARG;
    }
    char safe_key[KEY_LENGTH + 1];
    if (strlen(key) > KEY_LENGTH) {
        strncpy(safe_key, key, KEY_LENGTH);
        safe_key[KEY_LENGTH] = '\0';
        ESP_LOGW(TAG, "key length is too loog, original key=%s key=%s, may be cause error!", key, safe_key);
        key = safe_key;
    }
    
    if (value == nullptr) {
        ESP_LOGE(TAG, "写入数据指针为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_nvs || m_nvs->open_mode != NVS_READWRITE) {
        ESP_LOGE(TAG, "NVS只读或未打开");
        return ESP_FAIL;
    }

    std::unique_lock<std::mutex> lock(m_nvs->mutex, std::try_to_lock);
    if(!lock.owns_lock() || !is_valid()) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    auto err = nvs_set_str(m_nvs->handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写入%s失败: %s", key, esp_err_to_name(err));
    }
    return err;
}

// --- Blob写入 ---
esp_err_t MyNVS::write(const char* key, const void* value, size_t length)
{
    if (key == nullptr || *key == '\0') {
        ESP_LOGE(TAG, "键名为空");
        return ESP_ERR_INVALID_ARG;
    }
    char safe_key[KEY_LENGTH + 1];
    if (strlen(key) > KEY_LENGTH) {
        strncpy(safe_key, key, KEY_LENGTH);
        safe_key[KEY_LENGTH] = '\0';
        ESP_LOGW(TAG, "key length is too loog, original key=%s key=%s, may be cause error!", key, safe_key);
        key = safe_key;
    }
    
    if (value == nullptr) {
        ESP_LOGE(TAG, "写入数据指针为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_nvs || m_nvs->open_mode != NVS_READWRITE) {
        ESP_LOGE(TAG, "NVS只读或未打开");
        return ESP_FAIL;
    }
    std::unique_lock<std::mutex> lock(m_nvs->mutex, std::try_to_lock);
    if(!lock.owns_lock() || !is_valid()) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    auto err = nvs_set_blob(m_nvs->handle, key, value, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写入%s失败: %s", key, esp_err_to_name(err));
    }
    return err;
}



// --- 其他方法 ---
esp_err_t MyNVS::find(const char* key)
{
    nvs_type_t type;
    char safe_key[KEY_LENGTH + 1];
    if (strlen(key) > KEY_LENGTH) {
        strncpy(safe_key, key, KEY_LENGTH);
        safe_key[KEY_LENGTH] = '\0';
        ESP_LOGW(TAG, "key length is too loog, original key=%s key=%s, may be cause error!", key, safe_key);
        key = safe_key;
    }
    return find(key, &type);
}

esp_err_t MyNVS::find(const std::string& key)
{
    nvs_type_t type;
    return find(key.c_str(), &type);
}

esp_err_t MyNVS::find(const std::string& key, nvs_type_t* out_type)
{
    return find(key.c_str(), out_type);
}

esp_err_t MyNVS::find(const char* key, nvs_type_t* out_type)
{
    if (key == nullptr || *key == '\0') {
        ESP_LOGE(TAG, "键名为空");
        return ESP_ERR_INVALID_ARG;
    }
    char safe_key[KEY_LENGTH + 1];
    if (strlen(key) > KEY_LENGTH) {
        strncpy(safe_key, key, KEY_LENGTH);
        safe_key[KEY_LENGTH] = '\0';
        ESP_LOGW(TAG, "key length is too loog, original key=%s key=%s, may be cause error!", key, safe_key);
        key = safe_key;
    }
    
    if(!m_nvs) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    std::unique_lock<std::mutex> lock(m_nvs->mutex, std::try_to_lock);
    if(!lock.owns_lock() || !is_valid()) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    return nvs_find_key(m_nvs->handle, key, out_type);
}

esp_err_t MyNVS::erase_key(const char* key)
{
    if (key == nullptr || *key == '\0') {
        ESP_LOGE(TAG, "键名为空");
        return ESP_ERR_INVALID_ARG;
    }
    char safe_key[KEY_LENGTH + 1];
    if (strlen(key) > KEY_LENGTH) {
        strncpy(safe_key, key, KEY_LENGTH);
        safe_key[KEY_LENGTH] = '\0';
        ESP_LOGW(TAG, "key length is too loog, original key=%s key=%s, may be cause error!", key, safe_key);
        key = safe_key;
    }
    
    if(!m_nvs) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    std::unique_lock<std::mutex> lock(m_nvs->mutex, std::try_to_lock);
    if(!lock.owns_lock() || !is_valid()) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    auto err = nvs_erase_key(m_nvs->handle, key);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "擦除%s失败: %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t MyNVS::erase_key(const std::string& key)
{
    return erase_key(key.c_str());
}

esp_err_t MyNVS::erase_all()
{
    if(!m_nvs) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    std::unique_lock<std::mutex> lock(m_nvs->mutex, std::try_to_lock);
    if(!lock.owns_lock() || !is_valid()) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    auto err = nvs_erase_all(m_nvs->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "擦除所有内容失败: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t MyNVS::commit()
{
    if(!m_nvs) {
        ESP_LOGE(TAG, "NVS未正确打开或实例已失效");
        return ESP_FAIL;
    }
    std::unique_lock<std::mutex> lock(m_nvs->mutex, std::try_to_lock);
    if(!lock.owns_lock() || m_nvs->handle == 0) {
        ESP_LOGE(TAG, "尝试加锁失败或NVS已关闭");
        return ESP_FAIL;
    }
    auto err = nvs_commit(m_nvs->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提交失败: %s", esp_err_to_name(err));
    }
    return err;
}