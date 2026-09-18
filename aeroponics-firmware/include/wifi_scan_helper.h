#pragma once

#include <cstdint>
#include <cstddef>
#include "wifi_storage_types.h"

constexpr size_t MAX_SCAN_NETWORKS = 20;

struct DiscoveredNetwork {
    char ssid[WIFI_MAX_SSID_LEN + 1] = {};
    int8_t rssi = -127;
    bool is_open = false;
};

/**
 * Deduplicates raw scan networks by SSID (retaining the strongest RSSI entry),
 * filters out empty/hidden SSIDs, sorts them in descending order of RSSI,
 * and clamps the result to max_output.
 *
 * @param input Pointer to array of raw discovered networks.
 * @param input_count Total number of raw networks in input.
 * @param output Pointer to array for processed unique networks.
 * @param max_output Maximum elements available in the output buffer.
 * @return Number of unique, valid networks populated in output.
 */
size_t deduplicateAndSortScanResults(
    const DiscoveredNetwork* input,
    size_t input_count,
    DiscoveredNetwork* output,
    size_t max_output
);

/**
 * Safely serializes an array of DiscoveredNetwork into a compact JSON string buffer,
 * properly escaping JSON special characters in SSIDs to prevent malformed payloads.
 *
 * Example output:
 * [{"ssid":"FarmNet","rssi":-62,"open":false},{"ssid":"Guest","rssi":-75,"open":true}]
 *
 * @param networks Array of discovered networks.
 * @param count Number of elements in networks.
 * @param json_buf Destination buffer for JSON string.
 * @param buf_size Size in bytes of destination buffer.
 * @return Number of characters written (excluding null terminator), or 0 on buffer overflow.
 */
size_t serializeNetworksToJson(
    const DiscoveredNetwork* networks,
    size_t count,
    char* json_buf,
    size_t buf_size
);
