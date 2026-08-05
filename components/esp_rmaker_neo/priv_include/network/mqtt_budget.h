/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mqtt_budget.h
 * @brief MQTT budgetted manager implementation for RainMaker Neo.
 */

#ifndef __MQTT_BUDGET_INTERNAL_H__
#define __MQTT_BUDGET_INTERNAL_H__

/* Includes *******************************************************/

/* MQTT includes */
#include "osal_mqtt_prototypes.h"

/* Public function declarations ****************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set up the implementation as the MQTT budgetted manager, and initialise required peripherals.
 * This uses the provided original implementation under the hood, with budgetting applied to the publish function.
 * @param[in] original_setup_fn The original setup function to call.
 * @param[out] mqtt_impl The MQTT implementation to set up.
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t mqtt_budget_impl_setup(osal_mqtt_impl_setup_t original_setup_fn, osal_mqtt_impl_t *mqtt_impl);

/**
 * @brief De-initialize the MQTT budgetted manager, and de-initialize required peripherals.
 * This restores the original publish function.
 * @param[in] mqtt_impl The MQTT implementation to de-initialize.
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t mqtt_budget_impl_deinit(osal_mqtt_impl_t *mqtt_impl);

/**
 * @brief Start the MQTT budget reviver. This periodically adds a task to the work queue to revive the budget.
 * @note This should be called after mqtt_budget_impl_setup().
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t mqtt_budget_impl_start_reviver(void);

/**
 * @brief Stop the MQTT budget reviver. This cancels the periodic task to revive the budget.
 * @note This should be called before mqtt_budget_impl_deinit().
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t mqtt_budget_impl_stop_reviver(void);

#ifdef __cplusplus
}
#endif

#endif /* __MQTT_BUDGET_INTERNAL_H__ */
