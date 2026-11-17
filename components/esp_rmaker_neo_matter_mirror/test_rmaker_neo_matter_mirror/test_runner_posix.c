/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_mirror_transforms(void);
void test_mirror_table_matching(void);
void test_mirror_lowering_cct_light(void);
void test_mirror_lowering_switch(void);
void test_mirror_sync_outbound(void);
void test_mirror_sync_outbound_command_selection(void);
void test_mirror_sync_outbound_invoke_fallback(void);
void test_mirror_onboarding_matter_first(void);
void test_mirror_onboarding_already_commissioned(void);
void test_mirror_onboarding_onnetwork_is_open(void);
void test_mirror_onboarding_wait_times_out(void);
void test_mirror_onboarding_resamples_after_init(void);
void test_mirror_onboarding_window_reopen(void);
void test_mirror_onboarding_window_never(void);
void test_mirror_sync_inbound_loop_closure(void);
void test_mirror_sync_inbound_coalescing(void);
void test_mirror_inbound_transient_collapses(void);
void test_mirror_inbound_transient_converges_after_injection(void);
void test_mirror_inbound_repeat_after_outbound(void);
void test_mirror_seeds_cluster_shadows(void);
void test_mirror_endpoint_correlation(void);
void test_mirror_binding_lifecycle(void);
void test_mirror_remove_endpoint(void);
void test_mirror_remove_endpoint_drops_name_binding(void);
void test_mirror_remove_endpoint_inside_read_section(void);
void test_mirror_add_device_after_start(void);
void test_mirror_remove_device_with_inbound_pending(void);
void test_mirror_lowering_drops_unbound_endpoint(void);
void test_mirror_readd_device(void);
void test_mirror_readd_kept_id_refused(void);
void test_mirror_readd_after_reboot_adopts(void);
void test_mirror_other_device_on_mapped_endpoint(void);
void test_mirror_public_child_node(void);
void test_mirror_bridge_topology(void);
void test_mirror_bridge_reachable(void);
void test_mirror_bridge_off(void);
void test_mirror_public_add_remove(void);
void test_mirror_add_enable_fails(void);
void test_mirror_name_binding(void);
void test_mirror_identify_no_param(void);
void test_mirror_identify_with_param(void);
void test_mirror_identify_handler(void);
void test_mirror_unmapped_attr_ignored(void);
void test_mirror_startup_boot_values(void);
void test_mirror_startup_policies(void);
void test_mirror_color_transforms(void);
void test_mirror_color_conversion(void);
void test_mirror_color_light_lowering(void);
void test_mirror_hs_only_gating(void);
void test_mirror_xy_inbound_atomicity(void);
void test_mirror_xy_echo_and_loop_termination(void);
void test_mirror_hs_outbound_updates_xy(void);
void test_mirror_xy_storm_coalescing(void);
void test_mirror_color_mode(void);
void test_mirror_scale_transform(void);
void test_mirror_reported_capability(void);
void test_mirror_golden_vectors_transforms(void);
void test_mirror_golden_vectors_composites(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mirror_transforms);
    RUN_TEST(test_mirror_table_matching);
    RUN_TEST(test_mirror_lowering_cct_light);
    RUN_TEST(test_mirror_lowering_switch);
    RUN_TEST(test_mirror_sync_outbound);
    RUN_TEST(test_mirror_sync_outbound_command_selection);
    RUN_TEST(test_mirror_sync_outbound_invoke_fallback);
    RUN_TEST(test_mirror_onboarding_matter_first);
    RUN_TEST(test_mirror_onboarding_already_commissioned);
    RUN_TEST(test_mirror_onboarding_onnetwork_is_open);
    RUN_TEST(test_mirror_onboarding_wait_times_out);
    RUN_TEST(test_mirror_onboarding_resamples_after_init);
    RUN_TEST(test_mirror_onboarding_window_reopen);
    RUN_TEST(test_mirror_onboarding_window_never);
    RUN_TEST(test_mirror_sync_inbound_loop_closure);
    RUN_TEST(test_mirror_sync_inbound_coalescing);
    RUN_TEST(test_mirror_inbound_transient_collapses);
    RUN_TEST(test_mirror_inbound_transient_converges_after_injection);
    RUN_TEST(test_mirror_inbound_repeat_after_outbound);
    RUN_TEST(test_mirror_seeds_cluster_shadows);
    RUN_TEST(test_mirror_endpoint_correlation);
    RUN_TEST(test_mirror_binding_lifecycle);
    RUN_TEST(test_mirror_remove_endpoint);
    RUN_TEST(test_mirror_remove_endpoint_drops_name_binding);
    RUN_TEST(test_mirror_remove_endpoint_inside_read_section);
    RUN_TEST(test_mirror_add_device_after_start);
    RUN_TEST(test_mirror_remove_device_with_inbound_pending);
    RUN_TEST(test_mirror_lowering_drops_unbound_endpoint);
    RUN_TEST(test_mirror_readd_device);
    RUN_TEST(test_mirror_readd_kept_id_refused);
    RUN_TEST(test_mirror_readd_after_reboot_adopts);
    RUN_TEST(test_mirror_other_device_on_mapped_endpoint);
    RUN_TEST(test_mirror_public_child_node);
    RUN_TEST(test_mirror_bridge_topology);
    RUN_TEST(test_mirror_bridge_reachable);
    RUN_TEST(test_mirror_bridge_off);
    RUN_TEST(test_mirror_public_add_remove);
    RUN_TEST(test_mirror_add_enable_fails);
    RUN_TEST(test_mirror_name_binding);
    RUN_TEST(test_mirror_identify_no_param);
    RUN_TEST(test_mirror_identify_with_param);
    RUN_TEST(test_mirror_identify_handler);
    RUN_TEST(test_mirror_unmapped_attr_ignored);
    RUN_TEST(test_mirror_startup_boot_values);
    RUN_TEST(test_mirror_startup_policies);
    RUN_TEST(test_mirror_color_transforms);
    RUN_TEST(test_mirror_color_conversion);
    RUN_TEST(test_mirror_color_light_lowering);
    RUN_TEST(test_mirror_hs_only_gating);
    RUN_TEST(test_mirror_xy_inbound_atomicity);
    RUN_TEST(test_mirror_xy_echo_and_loop_termination);
    RUN_TEST(test_mirror_hs_outbound_updates_xy);
    RUN_TEST(test_mirror_xy_storm_coalescing);
    RUN_TEST(test_mirror_color_mode);
    RUN_TEST(test_mirror_scale_transform);
    RUN_TEST(test_mirror_reported_capability);
    RUN_TEST(test_mirror_golden_vectors_transforms);
    RUN_TEST(test_mirror_golden_vectors_composites);
    return UNITY_END();
}
