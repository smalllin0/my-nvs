/**
 * @file    my_nvs_manager.h
 * @brief   NVS 命名空间槽位管理器：进程内单例，按 (partition, name) 复用槽位。
 *
 * 设计要点：
 *   - 每个槽位持有 nvs_handle_t 与引用计数 ref；
 *   - Open 同 (partition, name) 时若已存在，ref++ 复用；
 *   - 只读槽位不允许升级为读写（避免并发下模式切换）；
 *   - 最后一个引用释放时才 nvs_commit + nvs_close；
 *   - 全局互斥 mutex_ 保护槽位数组本身；槽位内 mutex 保护 nvs_handle_t 操作。
 */

#ifndef _MY_NVS_MANAGER_H_
#define _MY_NVS_MANAGER_H_

#include <string>
#include <string_view>
#include <atomic>
#include <nvs_flash.h>
#include <sdkconfig.h>
#include <mutex>

#define INVALID_INDEX (-1)  ///< 无效槽位索引

/// 一个已打开的 NVS 命名空间槽位
struct my_nvs_t {
    std::string         partition{};                 ///< 分区名（如 "nvs"）
    std::string         name{};                      ///< 命名空间
    nvs_open_mode_t     mode{NVS_READONLY};          ///< 打开模式
    nvs_handle_t        handle{0};                   ///< NVS 句柄（0 表示未打开）
    std::atomic<int>    ref{0};                      ///< 引用计数
    std::mutex          mutex;                       ///< 保护 handle 的读写锁
};

using slot_index_t = int8_t;   ///< 槽位索引类型

/**
 * @brief NVS 命名空间管理器（进程内单例）
 */
class MyNVS_Manager {
public:
    /// @brief 获取全局单例；首次调用时执行 nvs_flash_init()
    /// @return 单例指针；初始化失败返回 nullptr
    static MyNVS_Manager* GetInstance();

    /// @brief 获取指定索引的槽位指针
    /// @return 槽位未打开（handle == 0）或索引越界时返回 nullptr
    my_nvs_t* GetNVS(slot_index_t index);

    /// @brief 打开命名空间：同 (partition, name) 已存在则复用并 ref++
    /// @return 槽位索引；失败返回 INVALID_INDEX
    slot_index_t Open(std::string_view partition,
                      std::string_view name,
                      nvs_open_mode_t mode = NVS_READONLY);

    /// @brief 关闭槽位：ref-- 到 0 时 commit + close
    void Close(my_nvs_t* nvs);

private:
    MyNVS_Manager()  = default;
    ~MyNVS_Manager() = default;

    /// @brief 按索引关闭（外部入口，加全局+槽位锁）
    void Close(slot_index_t index);

    /// @brief 关闭实现，要求调用方已持有相关锁
    void CloseSlotUnlocked(slot_index_t index);

    // static std::mutex               mutex_;                  ///< 保护 instance_ 与槽位数组
    // static inline MyNVS_Manager*    instance_{nullptr};               ///< 单例指针
    // static inline bool              init_flag_{false};              ///< nvs_flash_init() 是否已执行

    my_nvs_t                slot_[CONFIG_MAX_NAMESPACE];   ///< 固定容量槽位池
};

#endif /* _MY_NVS_MANAGER_H_ */