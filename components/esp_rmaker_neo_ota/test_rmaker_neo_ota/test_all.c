/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmng_ota_prototypes.h"

int test_rmng_ota_all_tests_unity(void)
{
    UNITY_BEGIN();

    /* Image progress tracking (progress.c) */
    RUN_TEST(test_progress_init_zero_filesize_returns_error);
    RUN_TEST(test_progress_init_null_ctx_returns_error);
    RUN_TEST(test_progress_init_sets_zero_received_and_correct_checkpoint);
    RUN_TEST(test_progress_seed_null_ctx_returns_error);
    RUN_TEST(test_progress_seed_sets_bytes_received);
    RUN_TEST(test_progress_seed_advances_checkpoint_past_seed);
    RUN_TEST(test_progress_seed_zero_equals_init);
    RUN_TEST(test_progress_seed_on_boundary_next_is_one_inc_ahead);
    RUN_TEST(test_progress_add_bytes_null_ctx_returns_error);
    RUN_TEST(test_progress_add_bytes_below_checkpoint_no_advance);
    RUN_TEST(test_progress_add_bytes_at_checkpoint_advances_next);
    RUN_TEST(test_progress_seed_then_add_small_no_advance);
    RUN_TEST(test_progress_seed_then_add_to_checkpoint_advances_once);

    /* OTA auto-resume NVS helpers (ota_nvs.c) */
    RUN_TEST(test_resume_nvs_save_load_roundtrip_descriptor);
    RUN_TEST(test_resume_nvs_save_load_roundtrip_tracker_blob);
    RUN_TEST(test_resume_nvs_load_absent_returns_not_found);
    RUN_TEST(test_resume_nvs_clear_after_save_makes_load_not_found);
    RUN_TEST(test_resume_nvs_clear_when_empty_is_noop);
    RUN_TEST(test_resume_matches_identical_descriptors_returns_true);
    RUN_TEST(test_resume_matches_different_md5_returns_false);
    RUN_TEST(test_resume_matches_different_filesize_returns_false);
    RUN_TEST(test_resume_matches_different_transport_returns_false);
    RUN_TEST(test_resume_matches_mqtt_different_block_size_returns_false);
    RUN_TEST(test_resume_matches_transport_none_returns_false);
    RUN_TEST(test_resume_matches_null_args_return_false);

    /* MQTT bitmap (mqtt_bitmap.c) */
    RUN_TEST(test_bitmap_init_exact_multiple_of_8);
    RUN_TEST(test_bitmap_init_non_multiple_has_padding_bits_set);
    RUN_TEST(test_bitmap_init_zero_block_count_returns_error);
    RUN_TEST(test_bitmap_init_null_bitmap_returns_error);
    RUN_TEST(test_bitmap_block_unprocessed_after_init);
    RUN_TEST(test_bitmap_set_processed_clears_bit_and_decrements_count);
    RUN_TEST(test_bitmap_set_processed_last_block_in_cell);
    RUN_TEST(test_bitmap_set_processed_double_process_returns_invalid_state);
    RUN_TEST(test_bitmap_out_of_range_block_id_returns_error);
    RUN_TEST(test_bitmap_deinit_zeros_struct);
    RUN_TEST(test_popcount_fresh_9_blocks_equals_9_not_10);
    RUN_TEST(test_popcount_after_4_of_9_processed_equals_5);
    RUN_TEST(test_popcount_all_processed_equals_0);
    RUN_TEST(test_popcount_exact_multiple_of_8_no_padding);

    RUN_TEST(test_fsm_uninitialized_any_event_returns_uninitialized);
    RUN_TEST(test_fsm_network_init_null_stays);
    RUN_TEST(test_fsm_network_init_wrong_event_stays);
    RUN_TEST(test_fsm_reboot_check_null_stays);
    RUN_TEST(test_fsm_reboot_check_other_event_stays);
    RUN_TEST(test_fsm_reboot_check_recoverable_reject_stays);
    RUN_TEST(test_fsm_reboot_check_unrecoverable_reject_returns_idle_and_clears_job);
    RUN_TEST(test_fsm_idle_fetch_requested_returns_fetching_pending_jobs);
    RUN_TEST(test_fsm_idle_jobs_changed_returns_jobs_changed);
    RUN_TEST(test_fsm_idle_other_events_stay_idle);
    RUN_TEST(test_fsm_jobs_changed_no_payload_returns_idle);
    RUN_TEST(test_fsm_jobs_changed_wrong_event_stay_jobs_changed);
    RUN_TEST(test_fsm_jobs_changed_invalid_json_enters_error);
    RUN_TEST(test_fsm_fetching_pending_jobs_non_empty_transition_stays);
    RUN_TEST(test_fsm_fetching_pending_jobs_null_stays);
    RUN_TEST(test_fsm_waiting_for_pending_jobs_accepted_returns_pending_jobs_received);
    RUN_TEST(test_fsm_waiting_for_pending_jobs_rejected_enters_error);
    RUN_TEST(test_fsm_waiting_for_pending_jobs_timeout_enters_error);
    RUN_TEST(test_fsm_waiting_for_pending_jobs_other_event_stays);
    RUN_TEST(test_fsm_waiting_for_pending_jobs_null_stays);
    RUN_TEST(test_fsm_pending_jobs_received_no_payload_enters_error);
    RUN_TEST(test_fsm_pending_jobs_received_wrong_event_stays);
    RUN_TEST(test_fsm_pending_jobs_received_null_stays);
    RUN_TEST(test_fsm_fetching_job_doc_null_stays);
    RUN_TEST(test_fsm_fetching_job_doc_other_event_stays);
    RUN_TEST(test_fsm_waiting_for_job_doc_rejected_enters_error);
    RUN_TEST(test_fsm_waiting_for_job_doc_timeout_enters_error);
    RUN_TEST(test_fsm_waiting_for_job_doc_accepted_returns_job_doc_received);
    RUN_TEST(test_fsm_waiting_for_job_doc_other_event_stays);
    RUN_TEST(test_fsm_waiting_for_job_doc_null_stays);
    RUN_TEST(test_fsm_job_doc_received_null_stays);
    RUN_TEST(test_fsm_job_doc_received_wrong_event_stays);
    RUN_TEST(test_fsm_job_doc_received_oversized_job_id_rejected);
    RUN_TEST(test_fsm_job_doc_received_missing_job_id_rejected);
    RUN_TEST(test_fsm_job_doc_received_shorter_job_id_terminated_correctly);
    RUN_TEST(test_fsm_job_doc_received_standard_path_shorter_job_id_terminated_correctly);
    RUN_TEST(test_fsm_job_execution_null_stays);
    RUN_TEST(test_fsm_job_execution_other_event_stays);
    RUN_TEST(test_fsm_post_download_null_stays);
    RUN_TEST(test_fsm_post_download_other_event_stays);
    RUN_TEST(test_fsm_error_recovery_requested_transitions_to_recovery_state);
    RUN_TEST(test_fsm_error_null_stays_error);
    RUN_TEST(test_fsm_error_error_occurred_stays_error);
    RUN_TEST(test_fsm_error_other_events_stay_error);
    RUN_TEST(test_fsm_enter_error_sets_last_error_and_recovery);
    RUN_TEST(test_fsm_reboot_check_final_status_publish_fail_recovery_state_reboot_check);
    RUN_TEST(test_fsm_post_download_final_status_publish_fail_recovery_state_idle);
    RUN_TEST(test_fsm_post_download_queued_final_status_publish_fail_recovery_state_post_download);
    RUN_TEST(test_fsm_final_status_publish_fail_consecutive_increments_backoff_delay);

    /* Static event pool + enqueue-failure propagation */
    RUN_TEST(test_event_pool_init_creates_queue_with_configured_size);
    RUN_TEST(test_event_pool_return_recycles_slot);
    RUN_TEST(test_event_pool_return_rejects_non_pool_pointer);
    RUN_TEST(test_copy_event_data_uses_pool_for_payload_free_event);
    RUN_TEST(test_copy_event_data_falls_back_to_heap_on_pool_exhaustion);
    RUN_TEST(test_copy_event_data_uses_heap_for_payload_events);
    RUN_TEST(test_fsm_idle_fetch_enqueue_failure_enters_error_state);
    RUN_TEST(test_fsm_waiting_for_pending_jobs_accepted_enqueue_failure_enters_error_state);
    RUN_TEST(test_copy_event_data_preserves_fixed_data_in_pool_path);
    RUN_TEST(test_copy_event_data_preserves_fixed_data_in_heap_path);
    RUN_TEST(test_copy_event_data_reboot_signal_survives_pool_exhaustion);
    RUN_TEST(test_fsm_post_download_succeeded_fixed_data_requests_reboot);
    RUN_TEST(test_fsm_post_download_succeeded_null_fixed_data_no_reboot);
    RUN_TEST(test_post_terminal_event_null_returns_invalid_arg);
    RUN_TEST(test_post_terminal_event_direct_delivery_schedules_no_backoff);
    RUN_TEST(test_post_terminal_event_enqueue_failure_schedules_backoff_redelivery);
    RUN_TEST(test_terminal_repost_task_redelivers_then_stops);
    RUN_TEST(test_terminal_repost_task_reschedules_on_repeated_failure);
    RUN_TEST(test_fsm_error_recovery_event_data_nulled_on_enqueue_failure);
    RUN_TEST(test_fsm_error_recovery_event_data_nulled_on_enqueue_success);

    /* Default filetype handler version parsing (patch-version regression) */
    RUN_TEST(test_ft_version_basic_xyz);
    RUN_TEST(test_ft_version_patch_bump_is_greater);
    RUN_TEST(test_ft_version_ordering);
    RUN_TEST(test_ft_version_two_field_equals_zero_patch);
    RUN_TEST(test_ft_version_extra_segments_ignored);
    RUN_TEST(test_ft_version_invalid_inputs_rejected);

    /* OTA status manager: dropped-terminal-response recovery */
    RUN_TEST(test_ota_status_retry_rearms_after_successful_publish);
    RUN_TEST(test_ota_status_resend_pending_republishes_cached_terminals);
    RUN_TEST(test_ota_status_resend_pending_noop_when_empty);
    RUN_TEST(test_ota_status_clear_job_entries_removes_cached_terminal);
    RUN_TEST(test_ota_status_update_message_includes_status_details);
    RUN_TEST(test_ota_status_update_message_without_status_details);

    /* Image handler chunk range check (image/handler.c) */
    RUN_TEST(test_image_handler_chunk_within_image_is_written);
    RUN_TEST(test_image_handler_chunk_past_image_end_is_rejected);
    RUN_TEST(test_image_handler_chunk_offset_overflow_is_rejected);

    return UNITY_END();
}
