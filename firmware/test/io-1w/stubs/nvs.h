#pragma once
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
#define ESP_ERR_INVALID_STATE 2
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef int nvs_handle_t;
typedef int esp_err_t;
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
void nvs_close(nvs_handle_t);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_get_u32(nvs_handle_t, const char *, uint32_t *);
esp_err_t nvs_set_u32(nvs_handle_t, const char *, uint32_t);
esp_err_t nvs_get_used_entry_count(nvs_handle_t, size_t *);
esp_err_t nvs_erase_all(nvs_handle_t);
#ifdef __cplusplus
}
#endif
