/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <string.h>

#include "protobuf-c/protobuf-c.h"
#include "chal_resp/pb-c.h"
#include "osal_mem_alloc.h"

static const uint8_t TEST_CMD_PAYLOAD[] = "challenge-payload";
static const uint8_t TEST_RESP_PAYLOAD[] = "response-payload";
static const char TEST_NODE_ID[] = "test-node-id";

void test_pb_c_cmd_challenge_roundtrip(void)
{
    RmakerChResp__CmdCRPayload cmd_payload = RMAKER_CH_RESP__CMD_CRPAYLOAD__INIT;
    cmd_payload.payload.data = (uint8_t *)TEST_CMD_PAYLOAD;
    cmd_payload.payload.len = sizeof(TEST_CMD_PAYLOAD) - 1;

    RmakerChResp__RMakerChRespPayload payload = RMAKER_CH_RESP__RMAKER_CH_RESP_PAYLOAD__INIT;
    payload.msg = RMAKER_CH_RESP__RMAKER_CH_RESP_MSG_TYPE__TypeCmdChallengeResponse;
    payload.status = RMAKER_CH_RESP__RMAKER_CH_RESP_STATUS__Success;
    payload.payload_case = RMAKER_CH_RESP__RMAKER_CH_RESP_PAYLOAD__PAYLOAD_CMD_CHALLENGE_RESPONSE_PAYLOAD;
    payload.cmdchallengeresponsepayload = &cmd_payload;

    uint8_t buffer[256] = {0};
    size_t packed_size = rmaker_ch_resp__rmaker_ch_resp_payload__pack(&payload, buffer);
    TEST_ASSERT_GREATER_THAN(0, packed_size);

    RmakerChResp__RMakerChRespPayload *unpacked = rmaker_ch_resp__rmaker_ch_resp_payload__unpack(NULL, packed_size, buffer);
    TEST_ASSERT_NOT_NULL(unpacked);
    TEST_ASSERT_EQUAL(payload.msg, unpacked->msg);
    TEST_ASSERT_EQUAL(payload.status, unpacked->status);
    TEST_ASSERT_EQUAL(payload.payload_case, unpacked->payload_case);
    TEST_ASSERT_NOT_NULL(unpacked->cmdchallengeresponsepayload);
    TEST_ASSERT_EQUAL(cmd_payload.payload.len, unpacked->cmdchallengeresponsepayload->payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cmd_payload.payload.data, unpacked->cmdchallengeresponsepayload->payload.data, cmd_payload.payload.len);

    rmaker_ch_resp__rmaker_ch_resp_payload__free_unpacked(unpacked, NULL);
}

void test_pb_c_resp_pack_to_buffer(void)
{
    RmakerChResp__RespCRPayload resp_payload = RMAKER_CH_RESP__RESP_CRPAYLOAD__INIT;
    resp_payload.payload.data = (uint8_t *)TEST_RESP_PAYLOAD;
    resp_payload.payload.len = sizeof(TEST_RESP_PAYLOAD) - 1;
    resp_payload.node_id = (char *)TEST_NODE_ID;

    RmakerChResp__RMakerChRespPayload payload = RMAKER_CH_RESP__RMAKER_CH_RESP_PAYLOAD__INIT;
    payload.msg = RMAKER_CH_RESP__RMAKER_CH_RESP_MSG_TYPE__TypeRespChallengeResponse;
    payload.status = RMAKER_CH_RESP__RMAKER_CH_RESP_STATUS__Fail;
    payload.payload_case = RMAKER_CH_RESP__RMAKER_CH_RESP_PAYLOAD__PAYLOAD_RESP_CHALLENGE_RESPONSE_PAYLOAD;
    payload.respchallengeresponsepayload = &resp_payload;

    // Use pack() which allocates the buffer for us
    size_t packed_size = rmaker_ch_resp__rmaker_ch_resp_payload__get_packed_size(&payload);
    TEST_ASSERT_GREATER_THAN(0, packed_size);

    uint8_t *packed_data = OSAL_CALLOC_EXTRAM(packed_size, sizeof(uint8_t));
    TEST_ASSERT_NOT_NULL_MESSAGE(packed_data, "Failed to allocate memory for packed data");

    size_t actual_packed_size = rmaker_ch_resp__rmaker_ch_resp_payload__pack(&payload, packed_data);
    TEST_ASSERT_EQUAL(packed_size, actual_packed_size);

    RmakerChResp__RMakerChRespPayload *unpacked = rmaker_ch_resp__rmaker_ch_resp_payload__unpack(NULL, packed_size, packed_data);
    TEST_ASSERT_NOT_NULL(unpacked);
    TEST_ASSERT_EQUAL(payload.payload_case, unpacked->payload_case);
    TEST_ASSERT_NOT_NULL(unpacked->respchallengeresponsepayload);
    TEST_ASSERT_EQUAL_STRING(TEST_NODE_ID, unpacked->respchallengeresponsepayload->node_id);
    TEST_ASSERT_EQUAL(resp_payload.payload.len, unpacked->respchallengeresponsepayload->payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(resp_payload.payload.data, unpacked->respchallengeresponsepayload->payload.data, resp_payload.payload.len);

    rmaker_ch_resp__rmaker_ch_resp_payload__free_unpacked(unpacked, NULL);
    free(packed_data);
}
