/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_task.c
 * @brief The one task every controller operation runs on.
 *
 * The task owns a work-queue instance, so the queue is the bounded buffer and its receive loop is
 * the serialization. Two things the generic queue does not provide live here: completion, so a
 * public call can block for a status, and inline re-entrancy, so a callback may call any controller
 * API without deadlocking against the task that has to run it.
 *
 * Keeping this off the shared work queue means a cloud round-trip never serializes against an OTA
 * image write.
 */

#include <stdlib.h>

#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_semaphore.h"
#include "osal_ticks.h"

#include "esp_rmaker_work_queue.h"

#include "controller_internal.h"
#include "sdkconfig.h"

static const char *TAG = "rmng_controller";

typedef struct {
    controller_job_fn_t fn;
    void *arg;
    osal_semaphore_handle_t done;   /**< NULL for a fire-and-forget job. */
    esp_rmaker_error_t result;
} ctrl_job_t;

static esp_rmaker_work_queue_handle_t s_wq;

static void __job_runner(void *priv)
{
    ctrl_job_t *job = (ctrl_job_t *)priv;
    esp_rmaker_error_t err = job->fn(job->arg);
    /* Deliver the change lists the job queued before its caller wakes. */
    controller_changes_drain();
    if (job->done) {
        /* The waiter owns the job; publish the status before waking it. */
        job->result = err;
        osal_semaphore_give(job->done);
        return;
    }
    free(job->arg);
    free(job);
}

/* A job flushed at disable: wake its waiter with an error, or free it. */
static void __job_discard(esp_rmaker_work_fn_t work_fn, void *priv)
{
    if (work_fn != __job_runner) {
        return;
    }
    ctrl_job_t *job = (ctrl_job_t *)priv;
    if (job->done) {
        job->result = ESP_RMAKER_INVALID_STATE;
        osal_semaphore_give(job->done);
        return;
    }
    free(job->arg);
    free(job);
}

esp_rmaker_error_t controller_task_start(void)
{
    if (!s_wq) {
        /* Created once and never freed: an instance lives for the process, so a producer can never
         * send to a freed queue. Reopened on the next enable. */
        const esp_rmaker_work_queue_config_t cfg = {
            .name = "rmng_ctrl_task",
            .queue_size = CONFIG_ESP_RMAKER_NEO_CONTROLLER_JOB_QUEUE_DEPTH,
            .task_stack = CONFIG_ESP_RMAKER_NEO_CONTROLLER_TASK_STACK_DEPTH,
            .task_priority = CONFIG_ESP_RMAKER_NEO_CONTROLLER_TASK_PRIORITY,
            .discard_fn = __job_discard,
        };
        s_wq = esp_rmaker_work_queue_create_inst(&cfg);
        if (!s_wq) {
            return ESP_RMAKER_NO_MEM;
        }
    }
    esp_rmaker_error_t err = esp_rmaker_work_queue_init_inst(s_wq);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    return esp_rmaker_work_queue_start_inst(s_wq);
}

void controller_task_stop(void)
{
    if (s_wq) {
        esp_rmaker_work_queue_deinit_inst(s_wq);
    }
}

bool controller_task_is_current(void)
{
    return esp_rmaker_work_queue_is_current_task_inst(s_wq);
}

esp_rmaker_error_t controller_task_run_sync(controller_job_fn_t fn, void *arg)
{
    if (!fn) {
        return ESP_RMAKER_INVALID_ARG;
    }
    /* After a disable the queue still exists but is parked: report it as before the first enable,
     * not as a full queue. */
    if (!s_wq || !g_controller_state.enabled) {
        return ESP_RMAKER_NOT_INITIALIZED;
    }
    /* Already on the controller task: run it here, or we would wait on ourselves. */
    if (controller_task_is_current()) {
        return fn(arg);
    }

    ctrl_job_t job = { .fn = fn, .arg = arg };
    job.done = osal_semaphore_create_binary();
    if (!job.done) {
        return ESP_RMAKER_NO_MEM;
    }
    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task_inst(
                                 s_wq, __job_runner, &job,
                                 osal_ticks_from_ms(CONTROLLER_JOB_SUBMIT_TIMEOUT_MS));
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Controller job queue is full or parked");
        err = ESP_RMAKER_TIMEOUT;
    } else if (osal_semaphore_take(job.done, OSAL_MAX_DELAY) != OSAL_ERR_OK) {
        err = ESP_RMAKER_FAIL;
    } else {
        err = job.result;
    }
    osal_semaphore_delete(job.done);
    return err;
}

esp_rmaker_error_t controller_task_submit_async(controller_job_fn_t fn, void *arg)
{
    if (!fn) {
        free(arg);
        return ESP_RMAKER_INVALID_ARG;
    }
    if (!s_wq) {
        free(arg);
        return ESP_RMAKER_NOT_INITIALIZED;
    }
    ctrl_job_t *job = OSAL_CALLOC_EXTRAM(1, sizeof(*job));
    if (!job) {
        free(arg);
        return ESP_RMAKER_NO_MEM;
    }
    job->fn = fn;
    job->arg = arg;
    /* Zero timeout: the submitter is the MQTT task, which must never block. */
    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task_inst(s_wq, __job_runner, job, 0);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Controller job queue full; dropping a job");
        free(arg);
        free(job);
    }
    return err;
}
