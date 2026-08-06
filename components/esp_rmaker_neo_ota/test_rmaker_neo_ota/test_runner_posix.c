/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_rmaker_neo_ota_prototypes.h"

void setUp(void)
{
    test_rmaker_neo_ota_jobs_setUp();
}

void tearDown(void)
{
    test_rmaker_neo_ota_jobs_tearDown();
}

int main(void)
{
    return test_rmaker_neo_ota_all_tests_unity();
}
