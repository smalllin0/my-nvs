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

#pragma once

#include <string>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "nvs_flash.h"
#include "sdkconfig.h"

#define INVALID_INDEX           -1  // 索引无效标识

struct my_nvs_t {
    std::string         partition;  // 分区名
    std::string         name_space; // 名字空间
    nvs_open_mode_t     open_mode;  // 打开模式
    nvs_handle_t        handle;     // 操作句柄
    std::mutex          mutex;      // 操作锁
    std::atomic<int>    ref;        // 引用计数
};

class MyNVS_Manager {
public:
    static MyNVS_Manager* get_instance();
    static void release_instance();
    my_nvs_t* get_nvs(int8_t index);
    int8_t open(const char* name_space, nvs_open_mode_t mode = NVS_READONLY);
    int8_t open(const char* name_space, bool rw) {
        return open(name_sapce, rw ? NVS_READWRITE : NVS_READONLY);
    }
    int8_t open(const char* partition, const char* name_space, nvs_open_mode_t mode = NVS_READONLY);
    int8_t open(const char* partition, const char* name_space, nvs_open_mode_t mode = NVS_READONLY) {
        return open(partiion, name_sapce, rw ? NVS_READWRITE : NVS_READONLY);
    }
    void close(my_nvs_t* my_nvs);
private:
    MyNVS_Manager();
    ~MyNVS_Manager();
    void close(int8_t index);

    static bool             m_init_flag;
    std::mutex              m_mutex;
    static std::mutex       m_instance_mutex;
    static MyNVS_Manager*   m_nvs_manager;
    my_nvs_t                m_nvs[CONFIG_MAX_NAMESPACE];
};