#include <esp_bt.h>
#include <nvs.h>

#include "hci_transport.h"

#include "esp_log.h"
#define TAG "hci_transport_esp32"

//=============================================================================================
//=============================================================================================
//  hci transport for esp32

// wrapper for vhci
struct {
    hci_on_packet_handler handler;
    void* handler_ref;
    hci_on_ready_to_send_handler ready_handler;
    void* ready_handler_ref;
    esp_vhci_host_callback_t _cb;
} _hci_transport;


hci_handle hci_open()
{
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG,"Bluetooth Controller initialize failed: %s", esp_err_to_name(ret));
        return NULL;
    }
	//Note: the mode is dependent on what's enabled in menuconfig; will fail if they differ
    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT); //ESP_BT_MODE_BTDM
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,"Bluetooth Controller enable failed: %s", esp_err_to_name(ret));
        return NULL;
    }


    _hci_transport._cb.notify_host_recv = [](uint8_t *data, uint16_t len) -> int {
        if (_hci_transport.handler)
            _hci_transport.handler(&_hci_transport,data,len,_hci_transport.handler_ref);
		return 0;
    };
    _hci_transport._cb.notify_host_send_available = []() {
        if (_hci_transport.ready_handler)
            _hci_transport.ready_handler(&_hci_transport,_hci_transport.ready_handler_ref);
    };

    esp_vhci_host_register_callback(&_hci_transport._cb); // open for buisness
    return &_hci_transport;
}

int hci_close(hci_handle h)
{
	esp_bt_controller_disable();
	
    return 0;
}

void hci_set_packet_handler(hci_handle h, hci_on_packet_handler p, void* ref)
{
    _hci_transport.handler = p;
    _hci_transport.handler_ref = ref;
}

void hci_set_ready_to_send_handler(hci_handle h, hci_on_ready_to_send_handler p, void* ref)
{
    _hci_transport.ready_handler = p;
    _hci_transport.ready_handler_ref = ref;
}

int  hci_send(hci_handle h, const uint8_t* data, int len)
{
    esp_vhci_host_send_packet((uint8_t*)data,len);
	return 0;
}

int  hci_send_available(hci_handle h)
{
    return esp_vhci_host_check_send_available();
}

// store and load link keys
uint32_t _nvs_handle = 0;
static uint32_t open_nvs()
{
    nvs_open("esp_8_bit", NVS_READWRITE, &_nvs_handle);
    if (!_nvs_handle)
      ESP_LOGE(TAG,"_nvs_handle open failed!\n");
#if 0
    nvs_iterator_t it = nvs_entry_find("nvs", "esp_8_bit", NVS_TYPE_ANY);
    while (it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        it = nvs_entry_next(it);
        ESP_LOGE(TAG,"key '%s', type '%d' \n", info.key, info.type);
    };
#endif
    return _nvs_handle;
}

int sys_get_pref(const char* key, char* value, int max_len)
{
    value[0] = 0;
    uint32_t h = open_nvs();
    if (!h)
        return 0;
    size_t s = max_len;
    if (ESP_OK == nvs_get_str(h, key, value, &s)) {
       // printf("sys_get_pref %s:%s %d\n",key,value,(int)s);
        return (int)s;
    }
    return 0;
}

void sys_set_pref(const char* key, const char* value)
{
    uint32_t h = open_nvs();
    if (!h)
        return;
    //printf("sys_set_pref %s:%s\n",key,value);
    if (ESP_OK == nvs_set_str(h, key, value))
        nvs_commit(h);
    else
        ESP_LOGE(TAG,"sys_set_pref %s:%s failed (key length <= 15?)\n",key,value);
}
