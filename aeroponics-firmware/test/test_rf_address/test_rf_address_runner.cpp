#include <unity.h>

void setUp(void);
void tearDown(void);

void test_modern_address_predicates_cover_gateway_and_nodes_1_to_15(void);
void test_modern_address_encoding_rejects_group_reserved_and_out_of_range_sources(void);
void test_modern_address_encoding_rejects_source_equal_target(void);
void test_modern_address_round_trips_representative_generated_frames(void);
void test_modern_address_round_trips_every_remote_node_to_gateway(void);
void test_mutated_generated_address_frame_reports_crc_or_validation_error(void);
void test_deterministic_malformed_frame_mutations_never_decode_ok(void);

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_modern_address_predicates_cover_gateway_and_nodes_1_to_15);
    RUN_TEST(test_modern_address_encoding_rejects_group_reserved_and_out_of_range_sources);
    RUN_TEST(test_modern_address_encoding_rejects_source_equal_target);
    RUN_TEST(test_modern_address_round_trips_representative_generated_frames);
    RUN_TEST(test_modern_address_round_trips_every_remote_node_to_gateway);
    RUN_TEST(test_mutated_generated_address_frame_reports_crc_or_validation_error);
    RUN_TEST(test_deterministic_malformed_frame_mutations_never_decode_ok);

    return UNITY_END();
}
