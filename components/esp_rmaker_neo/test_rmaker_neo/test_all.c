/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"
#include "sdkconfig.h"

int test_rmaker_neo_all_tests_unity(void)
{
    UNITY_BEGIN();

    /* Node model *****************************************************************/
    RUN_TEST(test_node_model_basic);
    RUN_TEST(test_device_create_rejects_path_separator);
    RUN_TEST(test_param_create_rejects_path_separator);
    RUN_TEST(test_device_create_rejects_long_id);
    RUN_TEST(test_param_create_rejects_long_id);
    RUN_TEST(test_utf8_check_cases);
    RUN_TEST(test_utf8_param_create);
    RUN_TEST(test_utf8_param_update);
    RUN_TEST(test_utf8_param_valid_values);
    RUN_TEST(test_utf8_device_attribute);
    RUN_TEST(test_utf8_node_attribute_and_tag);
    RUN_TEST(test_utf8_node_info);
    RUN_TEST(test_utf8_restore_invalid_stored_value);
    RUN_TEST(test_utf8_restore_valid_stored_value);
    RUN_TEST(test_utf8_cloud_write_keeps_reports_working);
    RUN_TEST(test_path_round_trip);
    RUN_TEST(test_path_to_update_id_invalid);
    RUN_TEST(test_node_model_error_paths);
    RUN_TEST(test_node_model_persistent_params);
    RUN_TEST(test_node_model_persistent_params_across_deinit);
    RUN_TEST(test_node_config_basic);
    RUN_TEST(test_node_config_param_metadata);
    RUN_TEST(test_node_model_device_add_remove_and_errors);
    RUN_TEST(test_node_model_attributes_and_tags);
    RUN_TEST(test_node_model_getters_and_info);
    RUN_TEST(test_node_model_get_node_params_multiple_data_types);
    RUN_TEST(test_device_model_error_paths);
    RUN_TEST(test_device_model_delete_guard);
    RUN_TEST(test_device_model_attrs_callbacks_getters);
    RUN_TEST(test_param_model_getters);
    RUN_TEST(test_introspect_enumeration);
    RUN_TEST(test_introspect_metadata_getters);
    RUN_TEST(test_introspect_param_update_observer);
    RUN_TEST(test_introspect_device_write_params);

    /* Factory reset participants **************************************************/
    RUN_TEST(test_factory_reset_participant_register_arg_validation);
    RUN_TEST(test_factory_reset_participant_register_rejects_second_rebooting);
    RUN_TEST(test_factory_reset_participant_register_table_full);
    RUN_TEST(test_factory_reset_participant_reregister_updates_in_place);
    RUN_TEST(test_factory_reset_participants_wiped_in_registration_order);
    RUN_TEST(test_factory_reset_participants_rebooting_wiped_last);
    RUN_TEST(test_factory_reset_participants_wipe_failure_does_not_stop_others);
    RUN_TEST(test_factory_reset_from_participant_skips_only_the_originator);
    RUN_TEST(test_factory_reset_from_participant_null_self_wipes_all);
    RUN_TEST(test_factory_reset_reentrant_request_is_a_noop);
    RUN_TEST(test_factory_reset_participants_unregister_preserves_order);
    RUN_TEST(test_factory_reset_runs_again_after_a_run_without_reboot);
    RUN_TEST(test_factory_reset_with_wipe_runs_extra_before_participants);
    RUN_TEST(test_factory_reset_with_wipe_nested_skips_extra);
    RUN_TEST(test_factory_reset_delayed_runs_participants_after_delay);
    RUN_TEST(test_system_ctrl_delayed_reset_repeat_replaces_pending);
    RUN_TEST(test_system_ctrl_delayed_resets_of_different_types_do_not_stack);
    RUN_TEST(test_param_model_create_bounds_and_update);
    RUN_TEST(test_param_model_same_value_update_skips_nvs);
    RUN_TEST(test_param_model_store_get_invalid_parent);
    RUN_TEST(test_param_model_error_paths);
    RUN_TEST(test_param_model_handle_set_payload);
    RUN_TEST(test_param_model_handle_set_payload_typed);
    RUN_TEST(test_param_model_int64);
    RUN_TEST(test_param_model_stored_blob_len);
    RUN_TEST(test_param_model_stored_type_mismatch);
    RUN_TEST(test_val_int64);

    /* Timeseries ******************************************************************/
    RUN_TEST(test_timeseries_init_deinit);
    RUN_TEST(test_timeseries_push_data_basic);
    RUN_TEST(test_timeseries_push_data_validation);
    RUN_TEST(test_timeseries_get_payload_basic);
    RUN_TEST(test_timeseries_get_payload_multiple);
    RUN_TEST(test_timeseries_get_payload_max_items);
    RUN_TEST(test_timeseries_uninitialized_access);
    RUN_TEST(test_timeseries_data_types);
    RUN_TEST(test_timeseries_error_paths);
    RUN_TEST(test_timeseries_queue_behavior);
    RUN_TEST(test_timeseries_failed_publish_keeps_queue_order);
    RUN_TEST(test_timeseries_bad_entry_is_dropped);

    /* Local control endpoint protocol *********************************************/
    RUN_TEST(test_local_ctrl_get_params_fragment_walk);
    RUN_TEST(test_local_ctrl_get_config_fragment_walk);
    RUN_TEST(test_local_ctrl_get_params_offset_without_reset_fails);
    RUN_TEST(test_local_ctrl_get_params_offset_beyond_total_fails);
    RUN_TEST(test_local_ctrl_get_data_type_mismatch_fails);
    RUN_TEST(test_local_ctrl_free_data_invalidates_cache);
    RUN_TEST(test_local_ctrl_get_params_malformed_frame_rejected);
    RUN_TEST(test_local_ctrl_set_params_invalid_json_keeps_session);
    RUN_TEST(test_local_ctrl_set_params_valid_json_succeeds);
    RUN_TEST(test_local_ctrl_get_params_without_node_fails);
#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_GET_FILTER
    RUN_TEST(test_local_ctrl_get_params_filter_selects_params);
    RUN_TEST(test_local_ctrl_get_params_filter_skips_unknown_and_duplicates);
    RUN_TEST(test_local_ctrl_get_params_filter_malformed_rejected);
#endif
#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG
    RUN_TEST(test_local_ctrl_set_frag_applies_json);
    RUN_TEST(test_local_ctrl_set_frag_bad_fragment_discards_transfer);
    RUN_TEST(test_local_ctrl_set_frag_session_close_frees_transfer);
    RUN_TEST(test_local_ctrl_set_frag_bad_json_fails);
    RUN_TEST(test_local_ctrl_set_frag_malformed_request);
#endif
#if !CONFIG_IDF_TARGET_LINUX
    RUN_TEST(test_local_ctrl_custom_endpoint_requires_running_service);
    RUN_TEST(test_local_ctrl_custom_endpoint_add_remove);
    RUN_TEST(test_local_ctrl_custom_endpoint_table_full);
    RUN_TEST(test_local_ctrl_custom_endpoint_removed_on_disable);
    RUN_TEST(test_local_ctrl_custom_endpoint_removed_while_chal_resp_runs);
#endif
#if CONFIG_ESP_RMAKER_NEO_ENABLE_PROV_LOCAL_CTRL
    RUN_TEST(test_prov_local_ctrl_advertises_capabilities);
    RUN_TEST(test_prov_local_ctrl_prov_end_frees_cache);
    RUN_TEST(test_prov_local_ctrl_deinit_removes_endpoints);
#endif
#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SECURITY_2 && !CONFIG_IDF_TARGET_LINUX
    RUN_TEST(test_local_ctrl_sec2_custom_pop_does_not_poison_cache);
    RUN_TEST(test_local_ctrl_sec2_generated_pop_is_cached);
    RUN_TEST(test_local_ctrl_sec2_resolve_rejects_invalid_args);
#endif

    /* Protobuf helpers ************************************************************/
    RUN_TEST(test_pb_c_cmd_challenge_roundtrip);
    RUN_TEST(test_claim_validate_data_bounds);
    RUN_TEST(test_claim_fragment_sequencing);
    RUN_TEST(test_claim_command_guards);
    RUN_TEST(test_claim_verify_request);

    RUN_TEST(test_prov_capabilities_merge_under_one_label);
    RUN_TEST(test_prov_labels_stay_separate);
    RUN_TEST(test_prov_add_rejects_incomplete_endpoints);
    RUN_TEST(test_prov_add_rejects_incomplete_capabilities);
    RUN_TEST(test_prov_add_same_endpoint_overrides);
    RUN_TEST(test_prov_del_preserves_order);
    RUN_TEST(test_prov_add_reports_a_full_registry);
    RUN_TEST(test_prov_readd_after_drain);
    RUN_TEST(test_pb_c_resp_pack_to_buffer);

    /* MQTT host migration *********************************************************/
    RUN_TEST(test_migration_rejects_empty_target);
    RUN_TEST(test_migration_same_host_does_nothing);
    RUN_TEST(test_migration_new_host_in_memory_only);
    RUN_TEST(test_migration_app_override_is_stored_host);
    RUN_TEST(test_migration_stored_node_id_needs_no_write);
    RUN_TEST(test_migration_cert_cn_needs_write);
    RUN_TEST(test_migration_no_node_id_no_cert_fails);
    RUN_TEST(test_migration_flow_valid_connect_writes_back);
    RUN_TEST(test_migration_flow_valid_no_connect_times_out);
    RUN_TEST(test_migration_flow_connect_cancels_timeout);
    RUN_TEST(test_migration_flow_reboot_before_connect_retries);
#ifndef ESP_PLATFORM
    RUN_TEST(test_migration_flow_pending_connect_writes_back);
    RUN_TEST(test_migration_flow_pending_no_connect_rolls_back);
    RUN_TEST(test_migration_flow_valid_after_start_no_connect_times_out);
    RUN_TEST(test_migration_flow_raw_mark_valid_connect_writes_back);
    RUN_TEST(test_migration_flow_power_loss_after_mark_valid_retries);
#endif /* ESP_PLATFORM */

    /* Utilities - checksum ***********************************************************/
    RUN_TEST(test_checksum_basic);
    RUN_TEST(test_checksum_compare_store);
    RUN_TEST(test_checksum_compare_invalid_key);
    RUN_TEST(test_checksum_store_invalid_key);

    /* Utilities - trigger codec ******************************************************/
    RUN_TEST(test_trigger_codec_value_bool);
    RUN_TEST(test_trigger_codec_value_int);
    RUN_TEST(test_trigger_codec_value_int64);
    RUN_TEST(test_trigger_codec_int_blob_unchanged);
    RUN_TEST(test_trigger_codec_value_float);
    RUN_TEST(test_trigger_codec_value_string);
    RUN_TEST(test_trigger_codec_value_object);
    RUN_TEST(test_trigger_codec_value_array);
    RUN_TEST(test_trigger_codec_preserves_fields);
    RUN_TEST(test_trigger_codec_enabled_defaults_true_when_omitted);
    RUN_TEST(test_trigger_codec_multiple_and_operators);
    RUN_TEST(test_trigger_codec_empty_array);
    RUN_TEST(test_trigger_codec_is_smaller_than_json);
    RUN_TEST(test_trigger_codec_encode_rejects_unknown_operator);
    RUN_TEST(test_trigger_codec_encode_rejects_missing_path);
    RUN_TEST(test_trigger_codec_encode_rejects_null_value);
    RUN_TEST(test_trigger_codec_iter_rejects_wrong_version);
    RUN_TEST(test_trigger_codec_iter_rejects_truncated_int64);
    RUN_TEST(test_trigger_codec_iter_rejects_truncated);
    RUN_TEST(test_trigger_codec_invalid_args);

    /* MQTT topics ******************************************************************/
    RUN_TEST(test_mqtt_topics_basic);
    RUN_TEST(test_mqtt_topics_errors);
    RUN_TEST(test_mqtt_topics_group_control);
    RUN_TEST(test_mqtt_topics_parse_group_control_subgroup);

    /* Event loop *******************************************************************/
    RUN_TEST(test_event_loop_init_and_handlers);

    /* Network common ****************************************************************/
    RUN_TEST(test_network_common_init_deinit);
    RUN_TEST(test_network_common_payload);
    RUN_TEST(test_network_common_payload_zero_length);

    /* Cloud events ******************************************************************/
    RUN_TEST(test_cloud_event_timesync_builder);
    RUN_TEST(test_cloud_event_timesync_builder_registered);
    RUN_TEST(test_cloud_event_timesync_response_sets_processed_bit);
    RUN_TEST(test_cloud_event_timesync_response_ignores_invalid_time);
    RUN_TEST(test_cloud_manager_failed_send_releases_context);
    RUN_TEST(test_cloud_manager_newer_send_supersedes_context);
    RUN_TEST(test_cloud_manager_response_during_failed_send);
    RUN_TEST(test_cloud_manager_response_without_status_reports_failure);
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG
    RUN_TEST(test_cloud_event_server_config_builder);
    RUN_TEST(test_cloud_event_server_config_build_nested);
    RUN_TEST(test_cloud_event_server_config_build_buffer_size);
    RUN_TEST(test_cloud_event_server_config_build_invalid_args);
    RUN_TEST(test_cloud_event_server_config_parse_nested);
    RUN_TEST(test_cloud_event_server_config_parse_partial);
    RUN_TEST(test_cloud_event_server_config_parse_too_long);
    RUN_TEST(test_cloud_event_server_config_value_limits);
    RUN_TEST(test_cloud_event_server_config_ignores_unrequested);
    RUN_TEST(test_cloud_event_server_config_missing_key);
    RUN_TEST(test_cloud_event_server_config_non_string_value);
    RUN_TEST(test_cloud_event_server_config_waits_for_response);
    RUN_TEST(test_cloud_event_server_config_error_reply_not_received);
    RUN_TEST(test_cloud_event_server_config_response_is_cached);
    RUN_TEST(test_cloud_event_server_config_missing_rmng_base);
    RUN_TEST(test_cloud_event_server_config_invalid_args);
    RUN_TEST(test_cloud_event_server_config_deinit_keeps_readers_safe);
    RUN_TEST(test_cloud_event_server_config_stop_wakes_reader);
#endif

    /* Network notify ****************************************************************/
    RUN_TEST(test_notify_init_deinit);
    RUN_TEST(test_notify_send_invalid_args);
    RUN_TEST(test_notify_send_payload_generator_fail);
    RUN_TEST(test_notify_send_push_no_node_rejected);
    RUN_TEST(test_notify_send_push_routes_by_update_id);
    RUN_TEST(test_notify_param_update_and_notify_publishes_push);

    /* Schedules ********************************************************************/
    RUN_TEST(test_schedule_port_release_bumps_generation);
    RUN_TEST(test_schedule_port_lookup_rejects_released_slot);
    RUN_TEST(test_schedule_port_lookup_rejects_out_of_range_index);
    RUN_TEST(test_schedule_port_stale_dispatch_does_not_fire_recycled_slot);
    RUN_TEST(test_schedule_port_pool_recycles_slots);
    RUN_TEST(test_schedule_port_pool_grows_then_reuses);
    RUN_TEST(test_schedule_port_cancel_waits_for_running_callback);
    RUN_TEST(test_schedules_parse_action_valid_light_power);
    RUN_TEST(test_schedules_parse_action_valid_nested_object);
    RUN_TEST(test_schedules_parse_action_missing_returns_null);
    RUN_TEST(test_schedules_parse_action_empty_object_returns_empty);
    RUN_TEST(test_schedules_parse_action_free_null_safe);
    RUN_TEST(test_schedules_details_disabled_schedule_skipped);
    RUN_TEST(test_schedules_details_mixed_enabled_disabled);
    RUN_TEST(test_schedules_details_all_no_id_count_zero);
    RUN_TEST(test_schedules_details_clear_after_skip_no_crash);
    RUN_TEST(test_schedules_details_reparse_after_skip_no_crash);
    RUN_TEST(test_schedules_details_empty_id_rejected);
    RUN_TEST(test_schedules_details_oversized_id_rejected);
    RUN_TEST(test_schedules_details_numeric_id_rejected);
    RUN_TEST(test_schedules_details_null_id_rejected);
    RUN_TEST(test_schedules_details_object_id_rejected);
    RUN_TEST(test_schedules_details_max_length_id_accepted);
    RUN_TEST(test_schedules_details_name_field_ignored);
    RUN_TEST(test_schedules_parse_trigger_relative);
    RUN_TEST(test_schedules_parse_trigger_days_of_week);
    RUN_TEST(test_schedules_parse_trigger_date);
    RUN_TEST(test_schedules_parse_trigger_date_no_month_mask_recurs_monthly);
    RUN_TEST(test_schedules_parse_trigger_date_arm_beats_weekday_arm);
    RUN_TEST(test_schedules_parse_trigger_months_without_day_ignored);
    RUN_TEST(test_schedules_parse_trigger_minutes_out_of_range_fails);
    RUN_TEST(test_schedules_parse_trigger_empty_array_fails);
    RUN_TEST(test_schedules_parse_trigger_missing_fails);
    RUN_TEST(test_schedules_parse_trigger_extra_entries_dropped);
    RUN_TEST(test_schedules_parse_trigger_days_missing_minutes_fails);
    RUN_TEST(test_schedules_parse_trigger_invalid_first_entry_fails);
    RUN_TEST(test_schedules_parse_trigger_invalid_type_fails);
    RUN_TEST(test_schedules_details_count_clamped_to_max);
    RUN_TEST(test_schedules_update_details_null_input_safe);
    RUN_TEST(test_schedules_parse_validity_full);
    RUN_TEST(test_schedules_parse_validity_missing_returns_ok);
    RUN_TEST(test_schedules_parse_validity_empty_object);
    RUN_TEST(test_schedules_parse_validity_large_timestamps);
    RUN_TEST(test_schedules_parse_validity_bogus_keys_only);
    RUN_TEST(test_schedules_parse_validity_only_start);
    RUN_TEST(test_schedules_parse_validity_only_end);
    RUN_TEST(test_schedules_parse_validity_wrong_types);
    RUN_TEST(test_schedules_parse_validity_negative_values);
    RUN_TEST(test_schedules_parse_validity_not_an_object);
    RUN_TEST(test_schedules_parse_action_key_order_independent);
    RUN_TEST(test_schedules_parse_action_whitespace_tolerant);
    RUN_TEST(test_schedules_parse_full_schedule_object);
    RUN_TEST(test_schedules_serialize_round_trip_days_of_week);
    RUN_TEST(test_schedules_serialize_round_trip_date_recurring);
    RUN_TEST(test_schedules_serialize_round_trip_date_year_bounded);
    RUN_TEST(test_schedules_serialize_relative_emits_computed_ts);
    RUN_TEST(test_schedules_serialize_empty_node_is_empty_array);
    RUN_TEST(test_schedules_serialize_round_trip_preserves_count);
    RUN_TEST(test_schedules_arm_records_next_fire_for_date_trigger);
    RUN_TEST(test_schedules_arm_drops_date_trigger_with_past_year);
    RUN_TEST(test_schedules_serialize_emits_ts_for_date_one_shot);
    RUN_TEST(test_schedules_serialize_omits_ts_for_repeating);
    RUN_TEST(test_schedules_replay_skips_one_shot_already_due);
    RUN_TEST(test_schedules_replay_keeps_one_shot_still_pending);
    RUN_TEST(test_schedules_arm_drops_year_bounded_repeating_after_expiry);
    RUN_TEST(test_schedules_arm_keeps_year_bounded_repeating_still_valid);
    RUN_TEST(test_schedules_fired_one_shot_removed_from_node_and_payload);
    RUN_TEST(test_schedules_remove_by_cloud_id_removes_one);
    RUN_TEST(test_schedules_remove_by_cloud_id_unknown_is_noop);
    RUN_TEST(test_schedules_remove_by_cloud_id_last_one_frees_array);
    RUN_TEST(test_schedules_remove_by_cloud_id_null_safe);
    RUN_TEST(test_schedules_one_shot_flagged_in_priv_data);
    RUN_TEST(test_schedules_reload_no_stored_details_is_noop);
    RUN_TEST(test_schedules_reload_null_node_rejected);
    RUN_TEST(test_schedules_drop_node_releases_handles);
    RUN_TEST(test_schedules_erase_node_releases_handles);
    RUN_TEST(test_schedules_drop_and_erase_null_node_safe);
#ifdef CONFIG_ESP_RMAKER_NEO_BRIDGE_ENABLE
#endif

    /* Automation *******************************************************************/
    RUN_TEST(test_automation_parse_details_null_invalid_arg);
    RUN_TEST(test_automation_parse_details_empty_invalid_arg);
    RUN_TEST(test_automation_parse_details_invalid_json_fail);
    RUN_TEST(test_automation_parse_details_whitespace_empty_array);
    RUN_TEST(test_automation_parse_details_whitespace_invalid_triggers);
    RUN_TEST(test_automation_parse_details_all_invalid_frees_list);
    RUN_TEST(test_automation_parse_details_valid_format);
    RUN_TEST(test_automation_parse_details_exceeds_maximum);
    RUN_TEST(test_automation_parse_details_boolean_non_eq_ne_operators_rejected);
    RUN_TEST(test_automation_parse_details_partial_failure_rolls_back);
    RUN_TEST(test_automation_parse_details_enabled_false_single_no_triggers);
    RUN_TEST(test_automation_parse_details_enabled_omitted_adds_trigger);
    RUN_TEST(test_automation_parse_details_int64);
    RUN_TEST(test_automation_parse_details_enabled_explicit_true_and_false_skips);

    /* Time service Timestamp *******************************************************/
#if CONFIG_ESP_RMAKER_NEO_TIME_CURRENT_TIME_PARAM
    RUN_TEST(test_time_timestamp_param_rejects_invalid);
    RUN_TEST(test_time_timestamp_param_ignored_when_synced);
#if !CONFIG_ESP_RMAKER_NEO_TIME_CLOCK_OWNER_EXTERNAL
    RUN_TEST(test_time_timestamp_param_applies_before_sntp_sync);
#endif
#endif

    /* System control ****************************************************************/
    /* System control tests removed due to potential interference with test execution */

    /* Local config *****************************************************************/
    RUN_TEST(test_local_config_group_info_format);
    RUN_TEST(test_local_config_group_info_empty);
    RUN_TEST(test_local_config_group_info_format_skips_empty_subgroups);
    RUN_TEST(test_local_config_group_info_parse_packs_subgroups);
    RUN_TEST(test_local_config_group_info_set_get);
    RUN_TEST(test_local_config_stored_group_getters);
    RUN_TEST(test_local_config_other_accessors);
    RUN_TEST(test_local_config_group_info_invalid_args);
    RUN_TEST(test_local_config_group_info_null_subgroups);
    RUN_TEST(test_local_config_alexa_flag_default);
    RUN_TEST(test_local_config_sched_trigger_details_roundtrip);
    RUN_TEST(test_local_config_parse_group_info_primary_only);
    RUN_TEST(test_local_config_parse_group_info_with_subgroups);
    RUN_TEST(test_local_config_parse_group_info_empty_and_null);
    RUN_TEST(test_local_config_parse_group_info_invalid_args);
    RUN_TEST(test_local_config_parse_group_info_roundtrip);
    RUN_TEST(test_local_config_parse_group_info_max_subgroups);
    RUN_TEST(test_local_config_parse_group_info_null_subgroups);

    /* State changes ****************************************************************/
    RUN_TEST(test_state_changes_init_deinit);
    RUN_TEST(test_state_changes_concurrent_mark_and_drain);
    RUN_TEST(test_state_report_success_rearms_late_update);
    RUN_TEST(test_state_update_id_compare_orders_by_address);
    RUN_TEST(test_get_all_rejects_null_ctx);
    RUN_TEST(test_get_all_self_ctx_returns_self_params_only);
#ifdef CONFIG_ESP_RMAKER_NEO_BRIDGE_ENABLE
    RUN_TEST(test_get_all_filters_by_child_ctx);
    RUN_TEST(test_get_all_unknown_ctx_returns_zero);
#endif

    /* Subgroup-aware dispatch filter *******************************************/
    RUN_TEST(test_node_is_in_subgroup_rejects_invalid_args);
    RUN_TEST(test_node_is_in_subgroup_self_membership);
#ifdef CONFIG_ESP_RMAKER_NEO_BRIDGE_ENABLE
    RUN_TEST(test_node_is_in_subgroup_bridge_child_membership);
#endif

    /* Core *************************************************************************/
    RUN_TEST(test_core_node_init_unclaimed_succeeds);
    RUN_TEST(test_core_node_init_registers_chal_resp);
    RUN_TEST(test_core_node_init_defers_mqtt_init);
#if !CONFIG_IDF_TARGET_LINUX
    RUN_TEST(test_local_endpoints_enable_before_start_defers);
#endif
    RUN_TEST(test_core_node_init_null_config);
    RUN_TEST(test_core_node_init_success);
    RUN_TEST(test_core_node_deinit_states);
    RUN_TEST(test_core_start_without_init);
    // RUN_TEST(test_core_start_stop);
    RUN_TEST(test_core_stop_without_start);
    RUN_TEST(test_core_online_status_conditions);
    RUN_TEST(test_core_sign_challenge_invalid_args);
    RUN_TEST(test_core_error_paths);

#ifdef CONFIG_ESP_RMAKER_NEO_BRIDGE_ENABLE
    /* Bridge *********************************************************************/
    RUN_TEST(test_bridge_topic_group_control_subgroup_wildcard);
    RUN_TEST(test_bridge_topic_bridges_to_cloud);
    RUN_TEST(test_bridge_topic_child_shadow_updates);
    RUN_TEST(test_bridge_suffix_validator);
    RUN_TEST(test_bridge_parse_child_from_from_cloud_topic);
    RUN_TEST(test_bridge_parse_child_and_shadow_from_params_topic);
    RUN_TEST(test_bridge_public_api_arg_validation);
    RUN_TEST(test_bridge_child_nvs_load_absent_returns_not_found);
    RUN_TEST(test_bridge_child_nvs_store_load_roundtrip);
    RUN_TEST(test_bridge_child_nvs_set_sched_ver_preserves_other_fields);
    RUN_TEST(test_bridge_child_nvs_set_sched_ver_creates_when_absent);
    RUN_TEST(test_bridge_child_nvs_set_node_config_writes_both);
    RUN_TEST(test_bridge_child_nvs_erase);
    RUN_TEST(test_bridge_child_nvs_invalidates_on_version_mismatch);
    RUN_TEST(test_bridge_child_nvs_invalidates_on_size_mismatch);
    RUN_TEST(test_bridge_child_nvs_long_local_id_no_truncation_collision);
    RUN_TEST(test_bridge_child_nvs_compute_key);
    RUN_TEST(test_bridge_child_nvs_invalid_arg);
    RUN_TEST(test_bridge_accessors_local_id);
    RUN_TEST(test_bridge_accessors_topic_ctx);
    RUN_TEST(test_bridge_accessors_get_node_id_for_node);
    RUN_TEST(test_bridge_accessors_child_from_ctx_roundtrip);
    RUN_TEST(test_bridge_accessors_child_from_ctx_after_invalidate);
    RUN_TEST(test_bridge_accessors_version_progress);
    RUN_TEST(test_bridge_accessors_find_by_thing_name_and_local_id);
    RUN_TEST(test_bridge_accessors_slot_pool_exhaustion);
    RUN_TEST(test_bridge_accessors_seed_child_arg_validation);
    RUN_TEST(test_bridge_accessors_before_init);
    RUN_TEST(test_bridge_parse_child_from_from_cloud_topic_edge_cases);
    RUN_TEST(test_bridge_parse_child_and_shadow_from_params_topic_edge_cases);
    RUN_TEST(test_bridge_suffix_validator_extended);
    RUN_TEST(test_node_config_pending_init_idempotent);
    RUN_TEST(test_node_config_pending_add_rejects_null_ctx);
    RUN_TEST(test_node_config_pending_add_idempotent);
    RUN_TEST(test_node_config_pending_remove_self_and_readd);
    RUN_TEST(test_node_config_pending_remove_unknown_noop);
    RUN_TEST(test_node_config_pending_clear_inflight_unknown_noop);
    RUN_TEST(test_node_config_pending_clear_all_inflight_safe);
#endif

    /* Host control ******************************************************************/
#if !CONFIG_ESP_RMAKER_NEO_HOST_CTRL
    RUN_TEST(test_host_ctrl_seq_response_untagged);
    RUN_TEST(test_host_ctrl_seq_response_echoes_id);
    RUN_TEST(test_host_ctrl_seq_take_strips_id);
    RUN_TEST(test_host_ctrl_seq_take_leaves_untagged_command);
    RUN_TEST(test_host_ctrl_seq_take_ignores_id_without_command);
    RUN_TEST(test_host_ctrl_param_value_int64);
#endif

#ifdef CONFIG_ESP_RMAKER_NEO_CONTROLLER
    /* Group controller. Every unit here is pure: the tests link the parsers and the payload
     * renderers without the agent, the signer or the network behind them. */
    RUN_TEST(test_controller_url_splits_host_and_stage_path);
    RUN_TEST(test_controller_url_without_path_yields_empty_base_path);
    RUN_TEST(test_controller_url_strips_one_trailing_slash);
    RUN_TEST(test_controller_url_without_scheme_is_tolerated);
    RUN_TEST(test_controller_url_truncates_an_oversized_host);
    RUN_TEST(test_controller_url_null_yields_empty_outputs);

    RUN_TEST(test_controller_shadow_name_plain_group);
    RUN_TEST(test_controller_shadow_name_joins_subgroups_with_a_dash);
    RUN_TEST(test_controller_shadow_name_sorts_the_subgroup_path);
    RUN_TEST(test_controller_shadow_name_ignores_a_duplicate_subgroup);
    RUN_TEST(test_controller_topic_shadow_get);
    RUN_TEST(test_controller_topic_node_params);
    RUN_TEST(test_controller_topic_filters_use_a_single_level_wildcard);
    RUN_TEST(test_controller_topic_group_control_without_subgroup);
    RUN_TEST(test_controller_topic_group_control_with_subgroup);
    RUN_TEST(test_controller_topic_rejects_an_undersized_buffer);
    RUN_TEST(test_controller_thing_name_extracted_from_shadow_topic);
    RUN_TEST(test_controller_thing_name_honours_topic_len_without_nul);
    RUN_TEST(test_controller_thing_name_rejects_a_foreign_prefix);
    RUN_TEST(test_controller_thing_name_rejects_a_missing_separator);
    RUN_TEST(test_controller_thing_name_rejects_an_empty_name);

    RUN_TEST(test_controller_shadow_collect_reports_each_name_once);
    RUN_TEST(test_controller_shadow_collect_honours_the_buffer_bound);
    RUN_TEST(test_controller_shadow_collect_reports_none_for_an_empty_list);
    RUN_TEST(test_controller_shadow_in_use_finds_a_name_the_list_holds);
    RUN_TEST(test_controller_shadow_in_use_rejects_a_name_the_migration_dropped);

    RUN_TEST(test_controller_expired_credentials_detected_in_a_403_body);
    RUN_TEST(test_controller_expired_credentials_not_reported_for_a_denial);
    RUN_TEST(test_controller_expired_credentials_honours_body_len);

    RUN_TEST(test_controller_group_info_reports_every_node);
    RUN_TEST(test_controller_group_info_reports_the_group_name);
    RUN_TEST(test_controller_group_info_tolerates_a_missing_group_name);
    RUN_TEST(test_controller_group_info_reports_subgroups_and_their_members);
    RUN_TEST(test_controller_group_info_tolerates_a_missing_subgroup_name);
    RUN_TEST(test_controller_group_info_stops_when_a_callback_stops);
    RUN_TEST(test_controller_group_info_reports_an_empty_group);
    RUN_TEST(test_controller_group_info_rejects_a_body_without_node_ids);

    RUN_TEST(test_controller_config_parses_devices_and_params);
    RUN_TEST(test_controller_config_parses_bounds_on_a_numeric_param);
    RUN_TEST(test_controller_config_parses_int64_bounds);
    RUN_TEST(test_controller_config_parses_services_into_the_same_list);
    RUN_TEST(test_controller_config_accepts_name_as_a_legacy_alias_for_id);
    RUN_TEST(test_controller_config_parses_the_ui_type_and_the_primary_param);
    RUN_TEST(test_controller_config_primary_param_reports_none_once_it_is_dropped);
    RUN_TEST(test_controller_config_reports_the_parents_of_a_param);
    RUN_TEST(test_controller_config_maps_every_ui_type);
    RUN_TEST(test_controller_config_maps_every_data_type);
    RUN_TEST(test_controller_config_reads_the_data_model);
    RUN_TEST(test_controller_config_update_keeps_surviving_handles_valid);
    RUN_TEST(test_controller_config_update_makes_a_dropped_device_stale);

    RUN_TEST(test_controller_state_applies_reported_params);
    RUN_TEST(test_controller_state_reports_not_found_before_any_value);
    RUN_TEST(test_controller_state_delta_keeps_params_it_does_not_mention);
    RUN_TEST(test_controller_state_reports_no_change_when_the_value_is_the_same);
    RUN_TEST(test_controller_state_ignores_the_desired_plane);
    RUN_TEST(test_controller_state_rejects_a_body_without_reported);
    RUN_TEST(test_controller_state_ncfg_ver_change_marks_the_config_stale);
    RUN_TEST(test_controller_state_config_is_current_until_ncfg_ver_moves);
    RUN_TEST(test_controller_state_reports_an_online_transition);
    RUN_TEST(test_controller_state_ncfg_ver_accepts_an_integer_token);
    RUN_TEST(test_controller_state_tracks_the_online_flag);
    RUN_TEST(test_controller_state_reports_every_changed_param);

    RUN_TEST(test_controller_write_node_payload_is_keyed_by_id);
    RUN_TEST(test_controller_write_refuses_a_read_only_param);
    RUN_TEST(test_controller_write_batch_groups_params_by_device);
    RUN_TEST(test_controller_write_node_batch_refuses_to_truncate);
    RUN_TEST(test_controller_write_batch_rejects_a_read_only_param);
    RUN_TEST(test_controller_write_batch_needs_a_begin);
    RUN_TEST(test_controller_write_group_payload_is_keyed_by_type);
    RUN_TEST(test_controller_write_group_payload_reaches_a_service_type);
    RUN_TEST(test_controller_write_group_payload_rejects_an_empty_type);
    RUN_TEST(test_controller_write_group_batch_holds_every_device_type);
    RUN_TEST(test_controller_write_group_batch_shares_one_envelope_per_type);
    RUN_TEST(test_controller_write_group_batch_needs_a_begin);
    RUN_TEST(test_controller_write_group_batch_refuses_to_truncate);
    RUN_TEST(test_controller_write_group_payload_refuses_invalid_utf8);
    RUN_TEST(test_controller_write_group_payload_refuses_to_truncate);

    RUN_TEST(test_controller_handle_resolves_only_its_own_kind_and_generation);
    RUN_TEST(test_controller_handle_of_a_reused_slot_does_not_match_the_old_handle);
    RUN_TEST(test_controller_handle_node_survives_a_resync_that_keeps_it);
    RUN_TEST(test_controller_handle_node_goes_stale_when_a_resync_drops_it);
    RUN_TEST(test_controller_handle_subgroup_survives_a_resync_and_takes_its_new_name);
    RUN_TEST(test_controller_handle_dropped_param_is_stale_and_a_restore_gets_a_new_one);
    RUN_TEST(test_controller_handle_param_is_stale_after_release_config);
    RUN_TEST(test_controller_handle_of_another_kind_is_an_invalid_arg);
    RUN_TEST(test_controller_handle_getter_returns_a_copy_the_caller_frees);
    RUN_TEST(test_controller_handle_device_param_by_id);
    RUN_TEST(test_controller_handle_notify_skips_a_released_param);
    RUN_TEST(test_controller_handle_batch_with_a_dropped_param_renders_nothing);
    RUN_TEST(test_controller_handle_parser_skips_fields_that_do_not_fit);
    RUN_TEST(test_controller_handle_parser_skips_a_name_that_does_not_fit);
    RUN_TEST(test_controller_handle_parser_skips_a_second_object_with_the_same_id);

    RUN_TEST(test_controller_changes_register_replays_the_snapshot);
    RUN_TEST(test_controller_changes_register_before_a_sync_replays_nothing);
    RUN_TEST(test_controller_changes_resync_reports_a_dropped_and_an_added_node);
    RUN_TEST(test_controller_changes_resync_reports_a_node_whose_subgroups_changed);
    RUN_TEST(test_controller_changes_refetch_reports_a_dropped_and_an_added_param);
    RUN_TEST(test_controller_changes_refetch_reports_changed_bounds);
    RUN_TEST(test_controller_changes_refetch_with_no_change_reports_nothing);
    RUN_TEST(test_controller_changes_release_config_reports_each_device);
    RUN_TEST(test_controller_changes_deinit_reports_each_node_and_subgroup);
    RUN_TEST(test_controller_changes_lists_arrive_in_order_and_old_ones_are_dropped);
    RUN_TEST(test_controller_changes_none_are_collected_without_a_callback);

    RUN_TEST(test_controller_lifecycle_sync_builds_the_group_with_values);
    RUN_TEST(test_controller_lifecycle_resync_of_an_unchanged_group_fetches_no_config);
    RUN_TEST(test_controller_lifecycle_delta_updates_one_value_and_notifies_it);
    RUN_TEST(test_controller_lifecycle_online_change_notifies_the_node);
    RUN_TEST(test_controller_lifecycle_config_change_refetches_and_seeds_the_new_params);
    RUN_TEST(test_controller_lifecycle_failed_refetch_keeps_the_config_stale);
    RUN_TEST(test_controller_lifecycle_write_publishes_by_id_and_refuses_what_it_must);
    RUN_TEST(test_controller_lifecycle_group_write_publishes_by_type);
    RUN_TEST(test_controller_lifecycle_migration_rebuilds_the_group_and_its_filters);
    RUN_TEST(test_controller_lifecycle_leaving_the_group_drops_the_inventory);
    RUN_TEST(test_controller_lifecycle_mqtt_down_fails_cleanly);
    RUN_TEST(test_controller_lifecycle_missing_config_reports_not_found);
    RUN_TEST(test_controller_lifecycle_api_get_returns_the_body);
    RUN_TEST(test_controller_lifecycle_disable_drops_the_shadow_filters);
    RUN_TEST(test_controller_lifecycle_disable_from_a_callback_is_refused);
    RUN_TEST(test_controller_lifecycle_disable_drops_an_open_group_batch);
    RUN_TEST(test_controller_lifecycle_blocking_call_after_disable_is_not_initialized);
    RUN_TEST(test_controller_lifecycle_disable_makes_every_handle_stale);
#endif

    return UNITY_END();
}
