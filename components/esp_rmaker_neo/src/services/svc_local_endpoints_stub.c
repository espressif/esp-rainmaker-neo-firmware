/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file services/svc_local_endpoints_stub.c
 * @brief Local endpoints service for targets without protocomm: no local control or challenge-response.
 */

/* Includes *******************************************************/

/* Declaration includes. */
#include "esp_rmaker_standard_services.h"

/* Public function definitions *******************************************************/

esp_rmaker_error_t esp_rmaker_local_ctrl_set_pop(const char *pop)
{
    (void)pop;
    return ESP_RMAKER_NOT_SUPPORTED;
}

esp_rmaker_error_t esp_rmaker_local_ctrl_set_http_port_from_host_ctrl(int port)
{
    (void)port;
    return ESP_RMAKER_NOT_SUPPORTED;
}

esp_rmaker_error_t esp_rmaker_local_ctrl_service_enable(void)
{
    return ESP_RMAKER_NOT_SUPPORTED;
}

esp_rmaker_error_t esp_rmaker_local_ctrl_service_disable(void)
{
    return ESP_RMAKER_NOT_SUPPORTED;
}

esp_rmaker_error_t esp_rmaker_local_ctrl_add_endpoint(const char *ep_name, esp_rmaker_local_ctrl_ep_handler_t handler,
        void *priv_data)
{
    (void)ep_name;
    (void)handler;
    (void)priv_data;
    return ESP_RMAKER_NOT_SUPPORTED;
}

esp_rmaker_error_t esp_rmaker_local_ctrl_remove_endpoint(const char *ep_name)
{
    (void)ep_name;
    return ESP_RMAKER_NOT_SUPPORTED;
}

esp_rmaker_error_t esp_rmaker_chal_resp_service_enable(void)
{
    return ESP_RMAKER_NOT_SUPPORTED;
}

esp_rmaker_error_t esp_rmaker_chal_resp_service_disable(void)
{
    return ESP_RMAKER_NOT_SUPPORTED;
}

bool esp_rmaker_chal_resp_service_is_enabled(void)
{
    return false;
}
