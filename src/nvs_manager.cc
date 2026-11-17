/**
 * @file    nvs_manager.cc
 * @brief   MyNVS_Manager 实现：nvs_flash_init 与槽位池的打开/复用/关闭。
 */

#include "my_nvs_manager.h"
#include "esp_log.h"

#define TAG "NVS_Manager"

static MyNVS_Manager* instance_{nullptr};
static bool init_flag_{false};
static std::mutex mutex_;

/// @brief 获取 NVS 管理实例（首次调用时执行 nvs_flash_init）
/// @return 失败时返回 nullptr
MyNVS_Manager* MyNVS_Manager::GetInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (instance_) return instance_;

    if (!init_flag_) {
        auto err = nvs_flash_init();
        // 分区满或版本变化：擦除后重新初始化
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS 分区需擦除并重新初始化: %s", esp_err_to_name(err));
            err = nvs_flash_erase();
            if (err == ESP_OK) {
                err = nvs_flash_init();
            }
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
            return nullptr;
        }
        init_flag_ = true;
    }

    instance_ = new (std::nothrow) MyNVS_Manager;
    return instance_;
}

/// @brief 获取指定索引的槽位指针
/// @return handle == 0（未打开）或索引越界时返回 nullptr
my_nvs_t* MyNVS_Manager::GetNVS(slot_index_t index)
{
    if (index < 0 || index >= CONFIG_MAX_NAMESPACE) {
        return nullptr;
    }
    return (slot_[index].handle != 0) ? &slot_[index] : nullptr;
}

/// @brief 打开指定分区和命名空间的 NVS
/// @return 槽位索引；失败返回 INVALID_INDEX
slot_index_t MyNVS_Manager::Open(std::string_view partition,
                                 std::string_view name,
                                 nvs_open_mode_t mode)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 第一遍：已存在相同 (partition, name) 则复用
    for (slot_index_t i = 0; i < CONFIG_MAX_NAMESPACE; i++) {
        auto& slot = slot_[i];
        if (slot.partition == partition && slot.name == name) {
            // 只读槽位不能升级为读写；读写槽位可接受任意模式
            if (slot.mode == NVS_READWRITE || mode == NVS_READONLY) {
                slot.ref.fetch_add(1, std::memory_order_relaxed);
                return i;
            }
            ESP_LOGE(TAG, "打开 [%s:%s] 失败：已有只读槽位无法升级为读写",
                     std::string(partition).c_str(), std::string(name).c_str());
            return INVALID_INDEX;
        }
    }

    // 第二遍：找空闲槽位真正打开
    for (slot_index_t i = 0; i < CONFIG_MAX_NAMESPACE; i++) {
        auto& slot = slot_[i];
        if (slot.partition.empty()) {
            nvs_handle_t handle = 0;
            auto err = nvs_open_from_partition(std::string(partition).c_str(),
                                               std::string(name).c_str(),
                                               mode, &handle);
            if (err == ESP_OK) {
                slot.partition = partition;
                slot.name      = name;
                slot.mode      = mode;
                slot.handle    = handle;
                slot.ref.store(1, std::memory_order_relaxed);
                return i;
            }
            ESP_LOGE(TAG, "打开 [%s:%s] 失败，错误码: %s",
                     std::string(partition).c_str(), std::string(name).c_str(),
                     esp_err_to_name(err));
            return INVALID_INDEX;
        }
    }

    ESP_LOGE(TAG, "槽位已满，当前最大支持 %d 个并发命名空间", CONFIG_MAX_NAMESPACE);
    return INVALID_INDEX;
}

/// @brief 按索引关闭槽位（外部入口，加全局 + 槽位双锁）
void MyNVS_Manager::Close(slot_index_t index)
{
    if (index < 0 || index >= CONFIG_MAX_NAMESPACE) {
        ESP_LOGE(TAG, "无效的索引 %d，忽略操作", index);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::lock_guard<std::mutex> lock_slot(slot_[index].mutex);
    CloseSlotUnlocked(index);
}

/// @brief 关闭实现：ref-- 到 0 时 commit + close 并清理槽位
void MyNVS_Manager::CloseSlotUnlocked(slot_index_t index)
{
    auto& slot = slot_[index];
    if (slot.ref.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        if (slot.handle) {
            nvs_commit(slot.handle);
            nvs_close(slot.handle);
            slot.handle = 0;
        }
        slot.partition.clear();
        slot.name.clear();
        slot.mode = NVS_READONLY;
        slot.ref.store(0, std::memory_order_release);
    }
}

/// @brief 按 my_nvs_t 指针关闭（指针必须位于槽位数组内）
void MyNVS_Manager::Close(my_nvs_t* nvs)
{
    if (nvs && nvs >= slot_ && nvs < slot_ + CONFIG_MAX_NAMESPACE) {
        Close(static_cast<slot_index_t>(nvs - slot_));
    }
}