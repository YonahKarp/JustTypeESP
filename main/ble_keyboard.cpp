#include "ble_keyboard.h"

#include <cctype>
#include <cstring>

#include "keyboard_input.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_hidh.h"
#include "esp_hidh_bluedroid.h"
#include "esp_hidh_gattc.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

namespace {

constexpr EventBits_t kScanDone = BIT0;
constexpr EventBits_t kConnected = BIT1;
constexpr EventBits_t kDisconnected = BIT2;
constexpr char kTag[] = "ble_keyboard";
EventGroupHandle_t state = nullptr;
esp_bd_addr_t candidate_address = {};
esp_ble_addr_type_t candidate_type = BLE_ADDR_TYPE_PUBLIC;
int candidate_rssi = -127;
bool have_candidate = false;

bool name_looks_keyboard(const char* name) {
    char lower[64] = {};
    for (size_t i = 0; i < sizeof(lower) - 1 && name[i]; ++i) {
        lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    }
    return std::strstr(lower, "keyboard") || std::strstr(lower, "keypad") ||
           std::strstr(lower, "kbd");
}

bool advertises_hid(const uint8_t* bytes, uint8_t length) {
    for (uint8_t i = 0; i + 1 < length; i += 2) {
        if (bytes[i] == 0x12 && bytes[i + 1] == 0x18) return true;
    }
    return false;
}

void scan_result(const esp_ble_gap_cb_param_t::ble_scan_result_evt_param& result) {
    const uint16_t length = result.adv_data_len + result.scan_rsp_len;
    uint8_t field_len = 0;
    uint8_t* uuids = esp_ble_resolve_adv_data_by_type(
        const_cast<uint8_t*>(result.ble_adv), length, ESP_BLE_AD_TYPE_16SRV_CMPL, &field_len);
    bool hid = uuids && advertises_hid(uuids, field_len);
    uuids = esp_ble_resolve_adv_data_by_type(
        const_cast<uint8_t*>(result.ble_adv), length, ESP_BLE_AD_TYPE_16SRV_PART, &field_len);
    hid |= uuids && advertises_hid(uuids, field_len);

    uint8_t* name_data = esp_ble_resolve_adv_data_by_type(
        const_cast<uint8_t*>(result.ble_adv), length, ESP_BLE_AD_TYPE_NAME_CMPL, &field_len);
    if (!name_data) name_data = esp_ble_resolve_adv_data_by_type(
        const_cast<uint8_t*>(result.ble_adv), length, ESP_BLE_AD_TYPE_NAME_SHORT, &field_len);
    char name[64] = {};
    if (name_data && field_len) {
        const size_t n = field_len < sizeof(name) - 1 ? field_len : sizeof(name) - 1;
        std::memcpy(name, name_data, n);
    }
    uint8_t appearance_len = 0;
    uint8_t* appearance_data = esp_ble_resolve_adv_data_by_type(
        const_cast<uint8_t*>(result.ble_adv), length, ESP_BLE_AD_TYPE_APPEARANCE, &appearance_len);
    const uint16_t appearance = appearance_data && appearance_len >= 2
        ? appearance_data[0] | (appearance_data[1] << 8) : 0;
    const bool keyboard = appearance == 0x03c1 || name_looks_keyboard(name);
    if (!hid && !keyboard) return;

    ESP_LOGI(kTag, "BLE %02x:%02x:%02x:%02x:%02x:%02x RSSI %d HID %d appearance 0x%04x name '%s'",
             result.bda[0], result.bda[1], result.bda[2], result.bda[3], result.bda[4],
             result.bda[5], result.rssi, hid, appearance, name);
    if (keyboard && (!have_candidate || result.rssi > candidate_rssi)) {
        std::memcpy(candidate_address, result.bda, sizeof(candidate_address));
        candidate_type = result.ble_addr_type;
        candidate_rssi = result.rssi;
        have_candidate = true;
    }
}

void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch (event) {
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(kTag, "Connection parameters: status %d, interval %.2f ms, latency %u",
                 param->update_conn_params.status,
                 param->update_conn_params.conn_int * 1.25,
                 param->update_conn_params.latency);
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            scan_result(param->scan_rst);
        } else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            xEventGroupSetBits(state, kScanDone);
        }
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        ESP_LOGW(kTag, "Type pairing code %06lu on the Bluetooth keyboard, then Enter",
                 static_cast<unsigned long>(param->ble_security.key_notif.passkey));
        break;
    case ESP_GAP_BLE_NC_REQ_EVT:
        ESP_LOGW(kTag, "Numeric comparison %06lu cannot be confirmed without user input",
                 static_cast<unsigned long>(param->ble_security.key_notif.passkey));
        esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, false);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ESP_LOGI(kTag, "Pairing %s", param->ble_security.auth_cmpl.success ? "succeeded" : "failed");
        break;
    default:
        break;
    }
}

void hid_callback(void*, esp_event_base_t, int32_t id, void* data) {
    auto* event = static_cast<esp_hidh_event_data_t*>(data);
    switch (static_cast<esp_hidh_event_t>(id)) {
    case ESP_HIDH_OPEN_EVENT:
        ESP_LOGI(kTag, "HID open: %s", event->open.status == ESP_OK ? "connected" : "failed");
        if (event->open.status == ESP_OK) {
            esp_ble_conn_update_params_t params = {};
            std::memcpy(params.bda, esp_hidh_dev_bda_get(event->open.dev), sizeof(params.bda));
            // BLE intervals are in 1.25 ms units: request 7.5–15 ms, no skipped events.
            params.min_int = 6;
            params.max_int = 12;
            params.latency = 0;
            params.timeout = 400;
            const esp_err_t result = esp_ble_gap_update_conn_params(&params);
            if (result != ESP_OK) ESP_LOGW(kTag, "Low-latency connection request failed: %s", esp_err_to_name(result));
            xEventGroupSetBits(state, kConnected);
        }
        else xEventGroupSetBits(state, kDisconnected);
        break;
    case ESP_HIDH_INPUT_EVENT:
        if (event->input.usage == ESP_HID_USAGE_KEYBOARD) {
            if (event->input.length != 8 && event->input.length != 9) {
                ESP_LOGW(kTag, "Unsupported keyboard report length %u (report ID %u)",
                         event->input.length, event->input.report_id);
            }
            keyboard_input::submit_report(event->input.data, event->input.length);
        }
        break;
    case ESP_HIDH_CLOSE_EVENT:
        ESP_LOGW(kTag, "HID disconnected (reason %d)", event->close.reason);
        keyboard_input::disconnected();
        xEventGroupSetBits(state, kDisconnected);
        break;
    default:
        break;
    }
}

void scan_task(void*) {
    esp_ble_scan_params_t params = {};
    params.scan_type = BLE_SCAN_TYPE_ACTIVE;
    params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    params.scan_interval = 0x50;
    params.scan_window = 0x30;
    params.scan_duplicate = BLE_SCAN_DUPLICATE_ENABLE;
    if (esp_ble_gap_set_scan_params(&params) != ESP_OK) {
        ESP_LOGE(kTag, "Could not set scan parameters");
        vTaskDelete(nullptr);
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    for (;;) {
        have_candidate = false;
        candidate_rssi = -127;
        xEventGroupClearBits(state, kScanDone | kConnected | kDisconnected);
        ESP_LOGI(kTag, "Scanning for BLE HID keyboards for 5 seconds");
        if (esp_ble_gap_start_scanning(5) != ESP_OK) {
            ESP_LOGE(kTag, "BLE scan start failed");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (!(xEventGroupWaitBits(state, kScanDone, pdTRUE, pdFALSE,
                                  pdMS_TO_TICKS(7000)) & kScanDone)) {
            ESP_LOGW(kTag, "BLE scan timed out");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        if (!have_candidate) {
            ESP_LOGI(kTag, "No BLE keyboard found; put keyboard in pairing mode");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        ESP_LOGI(kTag, "Connecting to candidate %02x:%02x:%02x:%02x:%02x:%02x",
                 candidate_address[0], candidate_address[1], candidate_address[2],
                 candidate_address[3], candidate_address[4], candidate_address[5]);
        if (!esp_hidh_dev_open(candidate_address, ESP_HID_TRANSPORT_BLE, candidate_type)) {
            ESP_LOGW(kTag, "HID connection request rejected");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        EventBits_t bits = xEventGroupWaitBits(state, kConnected | kDisconnected,
                                               pdTRUE, pdFALSE, pdMS_TO_TICKS(15000));
        if (bits & kConnected) {
            xEventGroupWaitBits(state, kDisconnected, pdTRUE, pdFALSE, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

}  // namespace

bool start_ble_keyboard() {
    if (nvs_flash_init() != ESP_OK) {
        ESP_LOGE(kTag, "NVS unavailable; leaving Bluetooth off (no erase attempted)");
        return false;
    }
    state = xEventGroupCreate();
    if (!state) return false;
    esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT) != ESP_OK ||
        esp_bt_controller_init(&controller) != ESP_OK ||
        esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) {
        ESP_LOGE(kTag, "Bluetooth controller startup failed");
        return false;
    }
    esp_bluedroid_config_t host = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if (esp_bluedroid_init_with_cfg(&host) != ESP_OK || esp_bluedroid_enable() != ESP_OK) {
        ESP_LOGE(kTag, "Bluetooth host startup failed");
        return false;
    }
    if (esp_ble_gap_register_callback(gap_callback) != ESP_OK ||
        esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler) != ESP_OK) {
        ESP_LOGE(kTag, "Bluetooth callbacks failed");
        return false;
    }
    // Allow older BLE keyboards that do not implement LE Secure Connections.
    esp_ble_auth_req_t auth = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t io = ESP_IO_CAP_OUT;
    uint8_t key_size = 16;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    esp_hidh_config_t config = {};
    config.callback = hid_callback;
    config.event_stack_size = 4096;
    if (esp_hidh_init(&config) != ESP_OK) {
        ESP_LOGE(kTag, "HID host startup failed");
        return false;
    }
    return xTaskCreate(scan_task, "ble_scan", 6144, nullptr, 2, nullptr) == pdPASS;
}
