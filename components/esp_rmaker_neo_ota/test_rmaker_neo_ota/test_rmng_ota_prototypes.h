/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_rmng_ota_prototypes.h
 * @brief Prototypes for the RainMaker Neo OTA test suite.
 */

#ifndef __TEST_RMNG_OTA_PROTOTYPES_H__
#define __TEST_RMNG_OTA_PROTOTYPES_H__

#include <stdint.h>

/* Test setup and teardown */
void rmng_ota_jobs_setUp(void);
void rmng_ota_jobs_tearDown(void);

#if defined(RMAKER_OTA_JOBS_TEST_WRAP_LINKER) || defined(RMAKER_OTA_JOBS_TEST_WRAP_DL_LIB)
/* Test hook: force OTA status publish (underlying MQTT) to fail when non-zero. Defined only in test build. */
void rmaker_ota_status_test_set_publish_fail(int fail);
/* Test hooks for the OTA status retry watchdog. Defined only in test build. */
void rmaker_ota_status_test_set_intercept_scheduling(int enable);
int rmaker_ota_status_test_get_retry_schedule_count(void);
void rmaker_ota_status_test_reset_retry_schedule_count(void);
void rmaker_ota_status_test_run_first_retry(void);
#endif

/* OTA status manager (ota_status.c) */
void test_ota_status_retry_rearms_after_successful_publish(void);
void test_ota_status_resend_pending_republishes_cached_terminals(void);
void test_ota_status_resend_pending_noop_when_empty(void);
void test_ota_status_clear_job_entries_removes_cached_terminal(void);
void test_ota_status_update_message_includes_status_details(void);
void test_ota_status_update_message_without_status_details(void);

/* Test helpers for backoff delay: reset to base delay and read current delay (ms). Defined only in test build. */
void rmaker_ota_jobs_test_reset_backoff(void);
uint64_t rmaker_ota_jobs_test_get_backoff_delay_ms(void);

/* FSM state / event behaviour */
void test_fsm_uninitialized_any_event_returns_uninitialized(void);
void test_fsm_network_init_null_stays(void);
void test_fsm_network_init_wrong_event_stays(void);
void test_fsm_reboot_check_null_stays(void);
void test_fsm_reboot_check_other_event_stays(void);
void test_fsm_reboot_check_recoverable_reject_stays(void);
void test_fsm_reboot_check_unrecoverable_reject_returns_idle_and_clears_job(void);
void test_fsm_idle_fetch_requested_returns_fetching_pending_jobs(void);
void test_fsm_idle_jobs_changed_returns_jobs_changed(void);
void test_fsm_idle_other_events_stay_idle(void);
void test_fsm_jobs_changed_no_payload_returns_idle(void);
void test_fsm_jobs_changed_wrong_event_stay_jobs_changed(void);
void test_fsm_jobs_changed_invalid_json_enters_error(void);
void test_fsm_fetching_pending_jobs_non_empty_transition_stays(void);
void test_fsm_fetching_pending_jobs_null_stays(void);
void test_fsm_waiting_for_pending_jobs_accepted_returns_pending_jobs_received(void);
void test_fsm_waiting_for_pending_jobs_rejected_enters_error(void);
void test_fsm_waiting_for_pending_jobs_timeout_enters_error(void);
void test_fsm_waiting_for_pending_jobs_other_event_stays(void);
void test_fsm_waiting_for_pending_jobs_null_stays(void);
void test_fsm_pending_jobs_received_no_payload_enters_error(void);
void test_fsm_pending_jobs_received_wrong_event_stays(void);
void test_fsm_pending_jobs_received_null_stays(void);
void test_fsm_fetching_job_doc_null_stays(void);
void test_fsm_fetching_job_doc_other_event_stays(void);
void test_fsm_waiting_for_job_doc_rejected_enters_error(void);
void test_fsm_waiting_for_job_doc_timeout_enters_error(void);
void test_fsm_waiting_for_job_doc_accepted_returns_job_doc_received(void);
void test_fsm_waiting_for_job_doc_other_event_stays(void);
void test_fsm_waiting_for_job_doc_null_stays(void);
void test_fsm_job_doc_received_null_stays(void);
void test_fsm_job_doc_received_wrong_event_stays(void);
void test_fsm_job_doc_received_oversized_job_id_rejected(void);
void test_fsm_job_doc_received_missing_job_id_rejected(void);
void test_fsm_job_doc_received_shorter_job_id_terminated_correctly(void);
void test_fsm_job_doc_received_standard_path_shorter_job_id_terminated_correctly(void);
void test_fsm_job_execution_null_stays(void);
void test_fsm_job_execution_other_event_stays(void);
void test_fsm_post_download_null_stays(void);
void test_fsm_post_download_other_event_stays(void);
void test_fsm_error_recovery_requested_transitions_to_recovery_state(void);
void test_fsm_error_null_stays_error(void);
void test_fsm_error_error_occurred_stays_error(void);
void test_fsm_error_other_events_stay_error(void);
void test_fsm_enter_error_sets_last_error_and_recovery(void);
void test_fsm_reboot_check_final_status_publish_fail_recovery_state_reboot_check(void);
void test_fsm_post_download_final_status_publish_fail_recovery_state_idle(void);
void test_fsm_post_download_queued_final_status_publish_fail_recovery_state_post_download(void);
void test_fsm_final_status_publish_fail_consecutive_increments_backoff_delay(void);

/* Static event pool + enqueue-failure propagation */
void test_event_pool_init_creates_queue_with_configured_size(void);
void test_event_pool_return_recycles_slot(void);
void test_event_pool_return_rejects_non_pool_pointer(void);
void test_copy_event_data_uses_pool_for_payload_free_event(void);
void test_copy_event_data_falls_back_to_heap_on_pool_exhaustion(void);
void test_copy_event_data_uses_heap_for_payload_events(void);
void test_fsm_idle_fetch_enqueue_failure_enters_error_state(void);
void test_fsm_waiting_for_pending_jobs_accepted_enqueue_failure_enters_error_state(void);
void test_fsm_error_recovery_event_data_nulled_on_enqueue_failure(void);
void test_fsm_error_recovery_event_data_nulled_on_enqueue_success(void);

/* fixed_data scalar + guaranteed terminal-event handoff */
void test_copy_event_data_preserves_fixed_data_in_pool_path(void);
void test_copy_event_data_preserves_fixed_data_in_heap_path(void);
void test_copy_event_data_reboot_signal_survives_pool_exhaustion(void);
void test_fsm_post_download_succeeded_fixed_data_requests_reboot(void);
void test_fsm_post_download_succeeded_null_fixed_data_no_reboot(void);
void test_post_terminal_event_null_returns_invalid_arg(void);
void test_post_terminal_event_direct_delivery_schedules_no_backoff(void);
void test_post_terminal_event_enqueue_failure_schedules_backoff_redelivery(void);
void test_terminal_repost_task_redelivers_then_stops(void);
void test_terminal_repost_task_reschedules_on_repeated_failure(void);

/* Default filetype handler version parsing (ota_filetype_handler_internal.c) */
void test_ft_version_basic_xyz(void);
void test_ft_version_patch_bump_is_greater(void);
void test_ft_version_ordering(void);
void test_ft_version_two_field_equals_zero_patch(void);
void test_ft_version_extra_segments_ignored(void);
void test_ft_version_invalid_inputs_rejected(void);

/* Image progress (progress.c) */
void test_progress_init_zero_filesize_returns_error(void);
void test_progress_init_null_ctx_returns_error(void);
void test_progress_init_sets_zero_received_and_correct_checkpoint(void);
void test_progress_seed_null_ctx_returns_error(void);
void test_progress_seed_sets_bytes_received(void);
void test_progress_seed_advances_checkpoint_past_seed(void);
void test_progress_seed_zero_equals_init(void);
void test_progress_seed_on_boundary_next_is_one_inc_ahead(void);
void test_progress_add_bytes_null_ctx_returns_error(void);
void test_progress_add_bytes_below_checkpoint_no_advance(void);
void test_progress_add_bytes_at_checkpoint_advances_next(void);
void test_progress_seed_then_add_small_no_advance(void);
void test_progress_seed_then_add_to_checkpoint_advances_once(void);

/* OTA auto-resume NVS helpers (ota_nvs.c) */
void test_resume_nvs_save_load_roundtrip_descriptor(void);
void test_resume_nvs_save_load_roundtrip_tracker_blob(void);
void test_resume_nvs_load_absent_returns_not_found(void);
void test_resume_nvs_clear_after_save_makes_load_not_found(void);
void test_resume_nvs_clear_when_empty_is_noop(void);
void test_resume_matches_identical_descriptors_returns_true(void);
void test_resume_matches_different_md5_returns_false(void);
void test_resume_matches_different_filesize_returns_false(void);
void test_resume_matches_different_transport_returns_false(void);
void test_resume_matches_mqtt_different_block_size_returns_false(void);
void test_resume_matches_transport_none_returns_false(void);
void test_resume_matches_null_args_return_false(void);

/* MQTT bitmap (mqtt_bitmap.c) */
void test_bitmap_init_exact_multiple_of_8(void);
void test_bitmap_init_non_multiple_has_padding_bits_set(void);
void test_bitmap_init_zero_block_count_returns_error(void);
void test_bitmap_init_null_bitmap_returns_error(void);
void test_bitmap_block_unprocessed_after_init(void);
void test_bitmap_set_processed_clears_bit_and_decrements_count(void);
void test_bitmap_set_processed_last_block_in_cell(void);
void test_bitmap_set_processed_double_process_returns_invalid_state(void);
void test_bitmap_out_of_range_block_id_returns_error(void);
void test_bitmap_deinit_zeros_struct(void);
void test_popcount_fresh_9_blocks_equals_9_not_10(void);
void test_popcount_after_4_of_9_processed_equals_5(void);
void test_popcount_all_processed_equals_0(void);
void test_popcount_exact_multiple_of_8_no_padding(void);

void test_image_handler_chunk_within_image_is_written(void);
void test_image_handler_chunk_past_image_end_is_rejected(void);
void test_image_handler_chunk_offset_overflow_is_rejected(void);

int test_rmng_ota_all_tests_unity(void);

#endif /* __TEST_RMNG_OTA_PROTOTYPES_H__ */
