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

#include <string.h>
#include "esp_log.h"
#include "my_nvs_manager.hpp"

#define TAG "MyNVS_Manager"

bool MyNVS_Manager::m_init_flag = false;
MyNVS_Manager* MyNVS_Manager::m_nvs_manager = nullptr;
std::mutex MyNVS_Manager::m_instance_mutex;

MyNVS_Manager* MyNVS_Manager::get_instance()
{
    std::lock_guard<std::mutex> lock(m_instance_mutex);
    if (m_nvs_manager == nullptr) {
        if (m_init_flag == false) {
            auto err = nvs_flash_init();
#if defined(CONFIG_ERASE_ON_NO_FREE_PAGES) || defined(CONFIG_ERASE_ON_NEW_VERSION_FOUND)
            if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
                ESP_ERROR_CHECK(nvs_flash_erase());
                ESP_LOGW(TAG, "由于%s,擦除NVS分区成功", esp_err_to_name(err));
                err = nvs_flash_init();
            }
#endif
            ESP_ERROR_CHECK(err);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "NVS初始化失败，错误码：%s", esp_err_to_name(err));
                return nullptr;
            }
            m_init_flag = true;
        }
        m_nvs_manager = new MyNVS_Manager;
        ESP_LOGI(TAG, "NVS 初始化完成.");
    }
    return m_nvs_manager;
}

void MyNVS_Manager::release_instance()
{
    std::lock_guard<std::mutex> lock(m_instance_mutex);
    if (m_nvs_manager != nullptr) {
        delete m_nvs_manager;
        m_nvs_manager = nullptr;
    }
}

MyNVS_Manager::MyNVS_Manager()
{
    for (auto &slot : m_nvs) {
        slot.partition.clear();
        slot.name_space.clear();
        slot.open_mode = NVS_READONLY;
        slot.handle = 0;
        slot.ref = 0;
    }
}

MyNVS_Manager::~MyNVS_Manager()
{
    std::lock_guard<std::mutex> lock_manager(m_mutex);
    for (auto &slot : m_nvs) {
        std::lock_guard<std::mutex> lock_nvs(slot.mutex);
        if (!slot.partition.empty()) {
            nvs_commit(slot.handle);
            nvs_close(slot.handle);
            slot.partition.clear();
            slot.name_space.clear();
            slot.handle = 0;
        }
    }
    m_nvs_manager = nullptr;
}

my_nvs_t* MyNVS_Manager::get_nvs(int8_t index)
{
    if (index < 0 || index >= CONFIG_MAX_NAMESPACE) {
        return nullptr;
    }
    return &(m_nvs[index]);
}

int8_t MyNVS_Manager::open(const char* partition, const char* name_space, nvs_open_mode_t mode)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int8_t i = 0; i < CONFIG_MAX_NAMESPACE; i++) {
        auto &slot = m_nvs[i];
        if ((slot.partition == partition) && (slot.name_space == name_space)) {
            if ((slot.open_mode == NVS_READWRITE) || (NVS_READONLY == mode)) {
                slot.ref.fetch_add(1);
                return i;
            } else {
                ESP_LOGE(TAG, "打开[分区:名字空间:模式]=[%s:%s:%s]失败，不再支持自动升级操作模式", partition, name_space, mode == NVS_READONLY ? "NVS_READONLY" : "NVS_READWRITE");
                return INVALID_INDEX;
            }
        }
    }
    for (int8_t i = 0; i < CONFIG_MAX_NAMESPACE; i++) {
        auto &slot = m_nvs[i];
        if (slot.partition.empty()) {
            auto err = nvs_open_from_partition(partition, name_space, mode, &(slot.handle));
            if (ESP_OK == err) {
                slot.partition = partition;
                slot.name_space = name_space;
                slot.open_mode = mode;
                slot.ref.store(1);
                return i;
            } else {
                ESP_LOGE(TAG, "打开[分区:命名空间]:[%s:%s]失败，错误码：%s.", partition, name_space, esp_err_to_name(err));
                return INVALID_INDEX;
            }
        }
    }
    ESP_LOGE(TAG, "槽位已满，请修改编译选项：MAX_NAMESPACE.");
    return INVALID_INDEX;
}

int8_t MyNVS_Manager::open(const char* name_space, nvs_open_mode_t mode)
{
    return open("nvs", name_space, mode);
}

void MyNVS_Manager::close(int8_t index)
{
    if (index < 0 || index >= CONFIG_MAX_NAMESPACE) {
        ESP_LOGE(TAG, "非法索引值，忽略关闭操作");
        return;
    }
    auto &slot = m_nvs[index];
    std::lock_guard<std::mutex> lock_manager(m_mutex);
    std::lock_guard<std::mutex> lock(slot.mutex);
    if (slot.ref.fetch_sub(1) == 1) {
        nvs_commit(slot.handle);
        nvs_close(slot.handle);
        slot.partition.clear();
        slot.name_space.clear();
        slot.open_mode = NVS_READONLY;
        slot.handle = 0;
        slot.ref = 0;
    }
}

void MyNVS_Manager::close(my_nvs_t* my_nvs)
{
    if (my_nvs == nullptr) {
        return;
    }
    close(static_cast<int8_t>(my_nvs - m_nvs));
}
