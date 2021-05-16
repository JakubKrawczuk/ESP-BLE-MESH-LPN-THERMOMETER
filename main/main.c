/* main.c - Application main entry point */

/*
 * Copyright (c) 2017 Intel Corporation
 * Additional Copyright (c) 2018 Espressif Systems (Shanghai) PTE LTD
 * Modified by Jakub Krawczuk
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_sensor_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"

#include "ble_mesh/callbacks.h"
#include "ble_mesh/bt.h"
#include "board.h"

#define TAG "MAIN"

#define NODE_NAME "THERMOMETER LPN"

/*
 * Company ID
 * List of company id's can be found here
 * https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers/
 */
#define CID_ESP     0x02E5 //Espressif Incorporated

/*
 * Universal Unique ID - 128bit
 * Static for now, will be altered using Bluetooth address during init
 */
static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = { 0x12, 0x34 };

/*
 * Sensor Property ID
 * List of sensor property id and corresponding variable types can be found here
 * https://www.bluetooth.com/specifications/specs/
 * Mesh Device Properties 2
 *
 * Sensor Property ID is a 2-octet value referencing a device property
 * that describes the meaning and format of data reported by a sensor.
 * 0x0000 is prohibited.
 */
#define SENSOR_PROPERTY_ID        0x005B  /* Present Outdoor Ambient Temperature */

/*
 * ONLY DEFAULT VALUE - changing this after sensor_init() won't affect server value
 *
 * The characteristic of device properties is "Temperature 8", which is
 * used to represent a measure of temperature with a unit of 0.5 degree Celsius.
 * Minimum value: -64.0, maximum value: 63.5.
 * A value of 0xFF represents 'value is not known'. - not sure about this
 * 0xFF -> -0.5 degree Celsius so better idea would be b10000000 -> 0x80 (it's u2 coded) -64 degree Celsius
 * EXAMPLE: value 60 - Outdoor temperature is 30 Degrees Celsius
 */
#define DEFAULT_OUTDOOR_TEMP 0x80

NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data, 1);

#define SENSOR_POSITIVE_TOLERANCE   ESP_BLE_MESH_SENSOR_UNSPECIFIED_POS_TOLERANCE
#define SENSOR_NEGATIVE_TOLERANCE   ESP_BLE_MESH_SENSOR_UNSPECIFIED_NEG_TOLERANCE
#define SENSOR_SAMPLE_FUNCTION      ESP_BLE_MESH_SAMPLE_FUNC_UNSPECIFIED
#define SENSOR_MEASURE_PERIOD       ESP_BLE_MESH_SENSOR_NOT_APPL_MEASURE_PERIOD
#define SENSOR_UPDATE_INTERVAL      ESP_BLE_MESH_SENSOR_NOT_APPL_UPDATE_INTERVAL

/*
 * CONFIG SERVER SETTINGS
 */

static esp_ble_mesh_cfg_srv_t config_server = {
#if defined(CONFIG_BLE_MESH_LOW_POWER)
	.relay = ESP_BLE_MESH_RELAY_NOT_SUPPORTED,
	.beacon = ESP_BLE_MESH_BEACON_DISABLED,
	.friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
	.gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#else
	#if defined(CONFIG_BLE_MESH_RELAY)
		.relay = ESP_BLE_MESH_RELAY_ENABLED,
	#else
    	.relay = ESP_BLE_MESH_RELAY_NOT_SUPPORTED,
	#endif
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
	#if defined(CONFIG_BLE_MESH_FRIEND)
    	.friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
	#else
		.friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
	#endif
	#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
		.gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
	#else
    	.gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
	#endif
#endif
    .default_ttl = 7,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

/*
 * BATTERY SERVER MODEL
 *
 */

ESP_BLE_MESH_MODEL_PUB_DEFINE(battery_pub, 4, ROLE_NODE);
static esp_ble_mesh_gen_battery_srv_t battery_server = {
		.rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
		.rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
		/*
		 * Battery state set here, can be changed
		 */
		.state.battery_level = 42,
		.state.time_to_discharge = 123,
};

static esp_ble_mesh_model_t battery_models[] = {
		ESP_BLE_MESH_MODEL_GEN_BATTERY_SRV(&battery_pub, &battery_server),
};

/*
 * SENSOR SERVER MODEL
 * todo: move to another file - waiting for response
 * https://devzone.nordicsemi.com/f/nordic-q-a/75209/sensor-server-get-message-send-to-root-element-address-should-be-send-to-2-element
 */

static esp_ble_mesh_sensor_state_t sensor_states[] = {
    /* Mesh Model Spec:
     * Multiple instances of the Sensor states may be present within the same model,
     * provided that each instance has a unique value of the Sensor Property ID to
     * allow the instances to be differentiated. Such sensors are known as multisensors.
     */
    [0] = {
        .sensor_property_id = SENSOR_PROPERTY_ID,
		/* Mesh Model Spec:
         * Sensor Descriptor state represents the attributes describing the sensor
         * data. This state does not change throughout the lifetime of an element.
         */
        .descriptor.positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
        .descriptor.negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
        .descriptor.sampling_function = SENSOR_SAMPLE_FUNCTION,
        .descriptor.measure_period = SENSOR_MEASURE_PERIOD,
        .descriptor.update_interval = SENSOR_UPDATE_INTERVAL,
        .sensor_data.format = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
        .sensor_data.length = 0, /* 0 represents the length is 1 */
        .sensor_data.raw_value = &sensor_data,
    },
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(sensor_pub, 20, ROLE_NODE);
static esp_ble_mesh_sensor_srv_t sensor_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state_count = ARRAY_SIZE(sensor_states),
    .states = sensor_states,
};

/*
 * sensor setup server should be present with sensor server
 */
ESP_BLE_MESH_MODEL_PUB_DEFINE(sensor_setup_pub, 20, ROLE_NODE);
static esp_ble_mesh_sensor_setup_srv_t sensor_setup_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state_count = ARRAY_SIZE(sensor_states),
    .states = sensor_states,
};

/*
 * ROOT MODELS
 */
static esp_ble_mesh_model_t root_models[]={
		ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
		ESP_BLE_MESH_MODEL_SENSOR_SRV(&sensor_pub, &sensor_server),
		ESP_BLE_MESH_MODEL_SENSOR_SETUP_SRV(&sensor_setup_pub, &sensor_setup_server),
};

/*
 * moved sensor server to root model - nRF Mesh app bug - sends get message to root even if in 2. element
 * https://devzone.nordicsemi.com/f/nordic-q-a/75209/sensor-server-get-message-send-to-root-element-address-should-be-send-to-2-element
static esp_ble_mesh_model_t sensor_models[]= {
	ESP_BLE_MESH_MODEL_SENSOR_SRV(&sensor_pub, &sensor_server),
	ESP_BLE_MESH_MODEL_SENSOR_SETUP_SRV(&sensor_setup_pub, &sensor_setup_server),
};
 */


static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
	ESP_BLE_MESH_ELEMENT(0, battery_models, ESP_BLE_MESH_MODEL_NONE),
	//ESP_BLE_MESH_ELEMENT(0, sensor_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
};

/*
 * Return provisioning bearers configured in sdkconfig
 */
uint8_t get_prov_bearer(){
	return
		#if defined(CONFIG_BLE_MESH_PB_ADV)
	    	ESP_BLE_MESH_PROV_ADV
		#else
			0
		#endif
		|
		#if defined(CONFIG_BLE_MESH_PB_GATT)
			ESP_BLE_MESH_PROV_GATT
		#else
		 0
		#endif
		;
}

/*
 * SET REPORTED TEMPERATURE
 * temp is real value multiplied by 2
 * range from -128 to 127 is mapped to temperature form -64.0 to 63.5 Celsius
 * Good idea would be pick one value as unknown - suggested b10000000 (-64)
 */
void set_temp(int8_t temp){
	*(sensor_data.data) = temp;
}

/*
 * SET BATTERY LEVEL
 */
void set_battery_status(uint8_t level, uint32_t ttd){
	battery_server.state.battery_level = level;
	battery_server.state.time_to_discharge = ttd; //ttd is 24bit value
	//...
}


static esp_err_t ble_mesh_init(void)
{
	/*
	 * Set default temperature reported by sensor
	 */
	net_buf_simple_add_u8(&sensor_data, DEFAULT_OUTDOOR_TEMP);

    esp_err_t err;

    /*
     * All done as auto response
     * could be omitted
     */
    esp_ble_mesh_register_prov_callback(provisioning_cb);
    esp_ble_mesh_register_config_server_callback(config_server_cb);
    esp_ble_mesh_register_sensor_server_callback(sensor_server_cb);

    //uuid need to be altered before
    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack");
        return err;
    }

    //advertised node name set
    esp_ble_mesh_set_unprovisioned_device_name(NODE_NAME);

    err = esp_ble_mesh_node_prov_enable(get_prov_bearer());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node");
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh initialized");

    return ESP_OK;
}

void app_main(void)
{
	ESP_ERROR_CHECK(nvs_flash_erase());
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing...");

    /*
     * Non volatile memory
     * prepare if needed
     */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    board_init();

    err = bluetooth_init();
    if (err) {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    //altering uuid
    ble_mesh_get_dev_uuid(dev_uuid);

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }
}
