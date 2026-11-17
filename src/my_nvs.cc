/**
 * @file    my_nvs.cc
 * @brief   MyNVS 中非模板成员函数的实现（字符串、原始 blob、find/erase/commit）。
 *
 * 约定：
 *   - 所有 key 先经 SafeKey 规范化；
 *   - 所有操作先取 Guard（加锁 + 检查句柄/模式）；
 *   - 返回 ESP-IDF 原生 esp_err_t，不做二次封装。
 */

#include "my_nvs.h"

#define TAG "MyNVS"


MyNVS::MyNVS(std::string_view partition, std::string_view name_space, nvs_open_mode_t mode)
{
    manager_ = MyNVS_Manager::GetInstance();
    SafeKey pk(partition);
    if (!pk) { ESP_LOGE(TAG, "分区名为空"); return; }
    SafeKey nk(name_space);
    if (!nk) { ESP_LOGE(TAG, "名称为空"); return; }

    auto index = manager_->Open(pk.c_str(), nk.c_str(), mode);
    if (index == INVALID_INDEX) { 
        nvs_ = nullptr;
        ESP_LOGE(TAG, "打开分区失败");
        return;
    }
    nvs_ = manager_->GetNVS(index);
}

MyNVS::~MyNVS()
{
    if (manager_ && nvs_) {
        manager_->Close(nvs_);
    }
    nvs_ = nullptr;
    manager_ = nullptr;
}








/// @brief 查询 key 是否存在及其 NVS 类型
esp_err_t MyNVS::find(std::string_view key, nvs_type_t& type)
{
    SafeKey sk(key);
    if (!sk) { ESP_LOGE(TAG, "键名为空"); return ESP_ERR_INVALID_ARG; }

    Guard g(nvs_, /*need_write=*/false);
    if (!g.ok) { ESP_LOGE(TAG, "NVS 未打开或实例已失效"); return ESP_ERR_INVALID_STATE; }

    return nvs_find_key(g.nvs->handle, sk.c_str(), &type);
}

/// @brief 删除单个 key
esp_err_t MyNVS::erase_key(std::string_view key)
{
    SafeKey sk(key);
    if (!sk) { ESP_LOGE(TAG, "键名为空"); return ESP_ERR_INVALID_ARG; }

    Guard g(nvs_, /*need_write=*/true);
    if (!g.ok) { ESP_LOGE(TAG, "NVS 未打开或只读"); return ESP_ERR_INVALID_STATE; }

    return nvs_erase_key(g.nvs->handle, sk.c_str());
}

/// @brief 清空当前命名空间全部 key
esp_err_t MyNVS::erase_all()
{
    Guard g(nvs_, /*need_write=*/true);
    if (!g.ok) { ESP_LOGE(TAG, "NVS 未打开或只读"); return ESP_ERR_INVALID_STATE; }

    return nvs_erase_all(g.nvs->handle);
}

/// @brief 提交挂起的写入
esp_err_t MyNVS::commit()
{
    Guard g(nvs_, /*need_write=*/true);
    if (!g.ok) { ESP_LOGE(TAG, "NVS 未打开或只读"); return ESP_ERR_INVALID_STATE; }

    return nvs_commit(g.nvs->handle);
}

/// @brief 读字符串（nvs_get_str 会写入 len 字节含 '\0'，读完缩到 len-1）
esp_err_t MyNVS::read(std::string_view key, std::string& value)
{
    SafeKey sk(key);
    if (!sk) { ESP_LOGE(TAG, "键名为空"); return ESP_ERR_INVALID_ARG; }

    Guard guard(nvs_, /*need_write=*/false);
    if (!guard.ok) { ESP_LOGE(TAG, "NVS 未打开或实例已失效"); return ESP_ERR_INVALID_STATE; }

    // 第一遍：查询所需长度（含结尾 '\0'）
    size_t len = 0;
    auto err = nvs_get_str(guard.nvs->handle, sk.c_str(), nullptr, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) return err;
    if (err != ESP_OK) { ESP_LOGE(TAG, "获取字符串长度失败: %s", esp_err_to_name(err)); return err; }
    if (len == 0) { value.clear(); return ESP_OK; }

    // 第二遍：按 len 分配再读，成功后去掉结尾 '\0'
    std::string tmp;
    tmp.resize(len);
    err = nvs_get_str(guard.nvs->handle, sk.c_str(), tmp.data(), &len);
    if (err == ESP_OK) {
        tmp.resize(len - 1);          // 去掉 '\0'
        value = std::move(tmp);
    } else {
        ESP_LOGE(TAG, "读取字符串失败");
    }
    return err;
}

/// @brief 写字符串
esp_err_t MyNVS::write(std::string_view key, const std::string& value)
{
    SafeKey sk(key);
    if (!sk) { ESP_LOGE(TAG, "键名为空"); return ESP_ERR_INVALID_ARG; }

    Guard g(nvs_, /*need_write=*/true);
    if (!g.ok) { ESP_LOGE(TAG, "NVS 未打开或只读"); return ESP_ERR_INVALID_STATE; }

    return nvs_set_str(g.nvs->handle, sk.c_str(), value.c_str());
}

/// @brief 读原始 blob
///        value == nullptr 时仅返回所需长度到 *length
esp_err_t MyNVS::read(std::string_view key, void* value, size_t* length)
{
    SafeKey sk(key);
    if (!sk) { ESP_LOGE(TAG, "键名为空"); return ESP_ERR_INVALID_ARG; }
    if (!length) { ESP_LOGE(TAG, "length 为空"); return ESP_ERR_INVALID_ARG; }

    Guard g(nvs_, /*need_write=*/false);
    if (!g.ok) { ESP_LOGE(TAG, "NVS 未打开或实例已失效"); return ESP_ERR_INVALID_STATE; }

    return nvs_get_blob(g.nvs->handle, sk.c_str(), value, length);
}

/// @brief 写原始 blob
esp_err_t MyNVS::write(std::string_view key, const void* value, size_t length)
{
    SafeKey sk(key);
    if (!sk) { ESP_LOGE(TAG, "键名为空"); return ESP_ERR_INVALID_ARG; }
    if (!value && length > 0) { ESP_LOGE(TAG, "写入数据指针为空"); return ESP_ERR_INVALID_ARG; }

    Guard g(nvs_, /*need_write=*/true);
    if (!g.ok) { ESP_LOGE(TAG, "NVS 未打开或只读"); return ESP_ERR_INVALID_STATE; }

    return nvs_set_blob(g.nvs->handle, sk.c_str(), value, length);
}

