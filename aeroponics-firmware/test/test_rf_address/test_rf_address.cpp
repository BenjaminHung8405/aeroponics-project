#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "config.h"
#include "rf_frame_codec.h"

namespace {

constexpr uint8_t kPsk[16] = {
    0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
    0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F};

size_t encodePing(uint8_t source, uint8_t target, uint16_t sequence,
                  uint8_t* frame, size_t frame_size) {
    const PingPayload payload{0x10203040U + sequence};
    const RfFrameMetadata metadata{source, target, 0x55667788U, sequence, 0x90000000U + sequence};
    return RfFrameCodec::encodeFrame(metadata, RfMessageType::PING, &payload, sizeof(payload),
                                      kPsk, sizeof(kPsk), frame, frame_size);
}

void assertPingRoundTrip(uint8_t source, uint8_t target, uint16_t sequence) {
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};
    const size_t frame_len = encodePing(source, target, sequence, frame, sizeof(frame));
    TEST_ASSERT_GREATER_THAN(0, frame_len);

    RfHeader header{};
    PingPayload decoded{};
    TEST_ASSERT_EQUAL(ParseError::OK,
                      RfFrameCodec::decodeFrameDetailed(frame, frame_len, kPsk, sizeof(kPsk),
                                                         header, &decoded, sizeof(decoded)));
    TEST_ASSERT_EQUAL_UINT8(source, header.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(target, header.target_node_id);
    TEST_ASSERT_EQUAL_UINT16(sequence, header.sequence);
    TEST_ASSERT_EQUAL_UINT32(0x10203040U + sequence, decoded.ping_timestamp_ms);
}

bool encodeWithAddresses(uint8_t source, uint8_t target) {
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};
    const PingPayload payload{0x12345678U};
    const RfFrameMetadata metadata{source, target, 1, 1, 1};
    return RfFrameCodec::encodeFrame(metadata, RfMessageType::PING, &payload, sizeof(payload),
                                      kPsk, sizeof(kPsk), frame, sizeof(frame)) != 0;
}

uint32_t nextDeterministic(uint32_t& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_modern_address_predicates_cover_gateway_and_nodes_1_to_15(void) {
    TEST_ASSERT_TRUE(RfFrameCodec::isValidAddress(RF_GATEWAY_NODE_ID));

    for (uint8_t node_id = RF_MIN_NODE_ID; node_id <= RF_MAX_NODE_ID; ++node_id) {
        TEST_ASSERT_TRUE(RfFrameCodec::isValidProductionRemoteNodeId(node_id));
        TEST_ASSERT_TRUE(RfFrameCodec::isValidAddress(node_id));
    }

    const uint8_t invalid_addresses[] = {0x10, 0x11, 0x14, 0x18, 0x1C, 0x1F, 0x20, 0xFF};
    for (uint8_t address : invalid_addresses) {
        TEST_ASSERT_FALSE(RfFrameCodec::isValidAddress(address));
        TEST_ASSERT_FALSE(RfFrameCodec::isValidProductionRemoteNodeId(address));
    }
}

void test_modern_address_encoding_rejects_group_reserved_and_out_of_range_sources(void) {
    const uint8_t invalid_sources[] = {0x10, 0x11, 0x14, 0x18, 0x1C, 0x1F, 0x20, 0xFF};
    for (uint8_t source : invalid_sources) {
        TEST_ASSERT_FALSE(encodeWithAddresses(source, RF_GATEWAY_NODE_ID));
    }

    const uint8_t invalid_targets[] = {0x10, 0x11, 0x14, 0x18, 0x1C, 0x1F, 0x20, 0xFF};
    for (uint8_t target : invalid_targets) {
        TEST_ASSERT_FALSE(encodeWithAddresses(RF_GATEWAY_NODE_ID, target));
    }
}

void test_modern_address_encoding_rejects_source_equal_target(void) {
    for (uint8_t address = RF_GATEWAY_NODE_ID; address <= RF_MAX_NODE_ID; ++address) {
        TEST_ASSERT_FALSE(encodeWithAddresses(address, address));
    }
}

void test_modern_address_round_trips_representative_generated_frames(void) {
    const uint8_t pairs[][2] = {{0, 1}, {1, 0}, {7, 15}, {15, 7}, {2, 14}};
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        assertPingRoundTrip(pairs[i][0], pairs[i][1], static_cast<uint16_t>(100 + i));
    }
}

void test_modern_address_round_trips_every_remote_node_to_gateway(void) {
    for (uint8_t node_id = RF_MIN_NODE_ID; node_id <= RF_MAX_NODE_ID; ++node_id) {
        assertPingRoundTrip(node_id, RF_GATEWAY_NODE_ID, node_id);
    }
}

void test_mutated_generated_address_frame_reports_crc_or_validation_error(void) {
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};
    const size_t frame_len = encodePing(15, RF_GATEWAY_NODE_ID, 77, frame, sizeof(frame));
    TEST_ASSERT_GREATER_THAN(0, frame_len);

    uint8_t mutated[RF_MAX_FRAME_SIZE] = {};
    std::memcpy(mutated, frame, frame_len);
    mutated[5] = 0x10;

    RfHeader header{};
    PingPayload decoded{};
    const ParseError error = RfFrameCodec::decodeFrameDetailed(
        mutated, frame_len, kPsk, sizeof(kPsk), header, &decoded, sizeof(decoded));
    TEST_ASSERT_NOT_EQUAL(ParseError::OK, error);
    TEST_ASSERT_EQUAL(ParseError::INVALID_ADDRESS, error);
}

void test_deterministic_malformed_frame_mutations_never_decode_ok(void) {
    constexpr size_t kMutationCount = 2500;
    uint32_t state = 0xC0DEC0DEU;
    size_t rejected = 0;

    for (size_t iteration = 0; iteration < kMutationCount; ++iteration) {
        const uint8_t source = static_cast<uint8_t>(1 + (nextDeterministic(state) % 15));
        const uint8_t target = RF_GATEWAY_NODE_ID;
        const uint16_t sequence = static_cast<uint16_t>(iteration + 1);
        uint8_t pristine[RF_MAX_FRAME_SIZE] = {};
        const size_t frame_len = encodePing(source, target, sequence, pristine, sizeof(pristine));
        TEST_ASSERT_GREATER_THAN(0, frame_len);

        uint8_t mutated[RF_MAX_FRAME_SIZE] = {};
        std::memcpy(mutated, pristine, frame_len);
        const size_t byte_index = nextDeterministic(state) % frame_len;
        const uint8_t bit = static_cast<uint8_t>(1U << (nextDeterministic(state) & 7U));
        mutated[byte_index] ^= bit;

        RfHeader header{};
        PingPayload decoded{};
        const ParseError error = RfFrameCodec::decodeFrameDetailed(
            mutated, frame_len, kPsk, sizeof(kPsk), header, &decoded, sizeof(decoded));
        TEST_ASSERT_NOT_EQUAL_MESSAGE(ParseError::OK, error,
                                      "deterministic mutated frame unexpectedly decoded OK");
        ++rejected;
    }

    TEST_ASSERT_EQUAL_UINT(kMutationCount, rejected);
}
