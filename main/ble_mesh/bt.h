/*
 * Copyright (c) 2017 Intel Corporation
 * Additional Copyright (c) 2020 Espressif Systems (Shanghai) PTE LTD
 * Modified by Jakub Krawczuk
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _BT_H_
#define _BT_H_

#include "esp_err.h"
#include "esp_log.h"
#include "esp_ble_mesh_defs.h"

void ble_mesh_get_dev_uuid(uint8_t *dev_uuid);

esp_err_t bluetooth_init(void);

#endif
