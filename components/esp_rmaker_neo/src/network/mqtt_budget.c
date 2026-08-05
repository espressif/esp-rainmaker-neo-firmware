/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mqtt_budget.c
 * @brief MQTT budgetted manager implementation for RainMaker Neo.
 */

/* Includes *******************************************************/

/* Declarations includes */
#include "network/mqtt_budget.h"

/* Standard includes */
#include <stddef.h>
#include <stdbool.h>

/* Platform common includes */
#include "osal_semaphore.h"
#include "osal_scheduler.h"
#include "osal_log.h"

/* Work queue includes */
#include "esp_rmaker_work_queue.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Constants **************************************************************/

/**
 * @brief Default MQTT budget.
 */
#define MQTT_BUDGET_DEFAULT_BUDGET (CONFIG_ESP_RMAKER_MQTT_DEFAULT_BUDGET)

/**
 * @brief Maximum MQTT budget.
 */
#define MQTT_BUDGET_MAX_BUDGET     (CONFIG_ESP_RMAKER_MQTT_MAX_BUDGET)

/**
 * @brief MQTT budget revive count.
 */
#define MQTT_BUDGET_REVIVE_COUNT   (CONFIG_ESP_RMAKER_MQTT_BUDGET_REVIVE_COUNT)

/**
 * @brief MQTT budget revive period.
 */
#define MQTT_BUDGET_REVIVE_PERIOD  (CONFIG_ESP_RMAKER_MQTT_BUDGET_REVIVE_PERIOD)

/**
 * @brief MQTT budget mutex take delay in milliseconds.
 */
#define MQTT_BUDGET_MUTEX_TAKE_DELAY_MS 100

/* Global variables **************************************************************/

/**
 * @brief Tag for the MQTT budget component.
 */
static const char *TAG = "rmng_net_mqtt_budget";

/**
 * @brief Original publish function.
 * This is the function that will be overridden by the budgetted manager.
 */
static osal_mqtt_publish_t osal_mqtt_publish_fn_original = NULL;

static struct {
    osal_semaphore_handle_t mutex; /* Mutex to guard the budget count */
    uint32_t budget; /* Current budget */
    osal_scheduler_task_handle_t revive_task_handle; /* Handle for the revive task */
} mqtt_budget_priv_data = {
    .mutex = NULL,
    .budget = 0,
    .revive_task_handle = NULL,
};
/* Private function declarations ****************************************************/

/**
 * @brief Lock the MQTT budget.
 * @return true on success, false otherwise.
 */
#define mqtt_budget_lock() (mqtt_budget_priv_data.mutex != NULL && osal_semaphore_take(mqtt_budget_priv_data.mutex, MQTT_BUDGET_MUTEX_TAKE_DELAY_MS) == OSAL_ERR_OK)

/**
 * @brief Unlock the MQTT budget.
 */
#define mqtt_budget_unlock() osal_semaphore_give(mqtt_budget_priv_data.mutex)

/**
 * @brief Check if the MQTT budget is available.
 * @return true if the budget is available, false otherwise.
 */
static bool mqtt_budget_check_available_and_decrement(void);

/**
 * @brief Initialise the MQTT budgetted manager peripherals.
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
static osal_err_t mqtt_budget_init_peripherals(void);

/**
 * @brief De-initialise the MQTT budgetted manager peripherals.
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
static osal_err_t mqtt_budget_deinit_peripherals(void);

/**
 * @brief Budgetted publish function.
 * @param[in] channel The channel to post the event to.
 * @param[in] topic The MQTT topic on which the message should be published.
 * @param[in] topic_len Length of the topic.
 * @param[in] data Data to be published.
 * @param[in] data_len Length of the data.
 * @param[in] qos Quality of service for the message.
 * @param[in] retain Whether the broker should retain the message.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
static osal_err_t mqtt_budget_publish_fn(osal_mqtt_event_loop_channel_t *channel,
        const char *topic,
        size_t topic_len,
        void *data,
        size_t data_len,
        osal_mqtt_QoS_t qos,
        bool retain);

/**
 * @brief Task to revive the MQTT budget.
 * @note This task is added to the work queue to revive the budget every MQTT_BUDGET_REVIVE_PERIOD seconds.
 * @param[in] arg unused
 */
static void mqtt_budget_revive_work_queue_task(void *arg);

/**
 * @brief Task to schedule the addition of the work queue task to revive the budget.
 * @note This task is scheduled periodically to revive the budget every MQTT_BUDGET_REVIVE_PERIOD seconds.
 * @param[in] arg unused
 */
static void mqtt_budget_revive_scheduler_task(void *arg);

/* Private function definitions ****************************************************/

static bool mqtt_budget_check_available_and_decrement(void)
{
    if (!mqtt_budget_lock()) {
        // treat failure to lock as available
        return true;
    }
    bool result = mqtt_budget_priv_data.budget > 0; // decrement and check if available
    if (result) {
        mqtt_budget_priv_data.budget--;
    }
    mqtt_budget_unlock();
    return result;
}

static osal_err_t mqtt_budget_init_peripherals(void)
{
    /* Create mutex */
    mqtt_budget_priv_data.mutex = osal_semaphore_create_mutex();
    if (mqtt_budget_priv_data.mutex == NULL) {
        return OSAL_ERR_NO_MEM;
    }

    /* Set budget count to initial default value */
    mqtt_budget_priv_data.budget = MQTT_BUDGET_DEFAULT_BUDGET;

    /* Return success */
    return OSAL_ERR_OK;
}

static osal_err_t mqtt_budget_deinit_peripherals(void)
{
    /* Delete mutex */
    osal_semaphore_delete(mqtt_budget_priv_data.mutex);
    mqtt_budget_priv_data.mutex = NULL;

    /* Reset budget count */
    mqtt_budget_priv_data.budget = 0;

    /* Return success */
    return OSAL_ERR_OK;
}

osal_err_t mqtt_budget_publish_fn(osal_mqtt_event_loop_channel_t *channel,
                                  const char *topic,
                                  size_t topic_len,
                                  void *data,
                                  size_t data_len,
                                  osal_mqtt_QoS_t qos,
                                  bool retain)
{
    /* Check if the budget is sufficient */
    if (!mqtt_budget_check_available_and_decrement()) {
        OSAL_LOGE(TAG, "Ran out of MQTT budget; cannot publish message");
        return OSAL_ERR_MQTT_SEND_FAILED;
    }

    /* Publish as original function */
    if (osal_mqtt_publish_fn_original != NULL) {
        return osal_mqtt_publish_fn_original(channel, topic, topic_len, data, data_len, qos, retain);
    }

    /* Should never happen */
    return OSAL_ERR_INVALID_STATE;
}

static void mqtt_budget_revive_work_queue_task(void *arg)
{
    (void)arg;
    if (mqtt_budget_priv_data.mutex == NULL) {
        return;
    }
    if (!mqtt_budget_lock()) {
        return;
    }
    mqtt_budget_priv_data.budget += MQTT_BUDGET_REVIVE_COUNT;
    if (mqtt_budget_priv_data.budget > MQTT_BUDGET_MAX_BUDGET) {
        mqtt_budget_priv_data.budget = MQTT_BUDGET_MAX_BUDGET;
    }
    mqtt_budget_unlock();
}

static void mqtt_budget_revive_scheduler_task(void *arg)
{
    (void)arg;
    esp_rmaker_work_queue_add_task(mqtt_budget_revive_work_queue_task, NULL);
}

/* Public function definitions ****************************************************/

osal_err_t mqtt_budget_impl_setup(osal_mqtt_impl_setup_t original_setup_fn, osal_mqtt_impl_t *mqtt_impl)
{
    /* Get osal_mqtt implementation */
    osal_err_t status = original_setup_fn(mqtt_impl);
    if (status != OSAL_ERR_OK) {
        return status;
    }

    /* Initialise peripherals */
    status = mqtt_budget_init_peripherals();
    if (status != OSAL_ERR_OK) {
        return status;
    }

    /* Override publish function */
    osal_mqtt_publish_fn_original = mqtt_impl->publish;
    mqtt_impl->publish = mqtt_budget_publish_fn;

    /* Return success */
    return OSAL_ERR_OK;
}

osal_err_t mqtt_budget_impl_deinit(osal_mqtt_impl_t *mqtt_impl)
{
    /* Deinit peripherals */
    osal_err_t status = mqtt_budget_deinit_peripherals();
    if (status != OSAL_ERR_OK) {
        return status;
    }

    /* Restore original publish function */
    mqtt_impl->publish = osal_mqtt_publish_fn_original;
    osal_mqtt_publish_fn_original = NULL;

    /* Return success */
    return OSAL_ERR_OK;
}

osal_err_t mqtt_budget_impl_start_reviver(void)
{
    /* Start revive scheduler task */
    return osal_scheduler_schedule_task_periodic(
               &mqtt_budget_priv_data.revive_task_handle,
               MQTT_BUDGET_REVIVE_PERIOD * 1000,  // convert to ms
               mqtt_budget_revive_scheduler_task,
               NULL
           );
}

osal_err_t mqtt_budget_impl_stop_reviver(void)
{
    /* Stop revive scheduler task */
    if (mqtt_budget_priv_data.revive_task_handle == NULL) {
        return OSAL_ERR_INVALID_STATE;
    }
    return osal_scheduler_cancel_task(&mqtt_budget_priv_data.revive_task_handle);
}
