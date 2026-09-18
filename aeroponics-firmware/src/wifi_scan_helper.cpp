#include "wifi_scan_helper.h"

#include <cstring>
#include <algorithm>
#include <cstdio>

size_t deduplicateAndSortScanResults(
    const DiscoveredNetwork* input,
    size_t input_count,
    DiscoveredNetwork* output,
    size_t max_output
) {
    if (!input || !output || max_output == 0 || input_count == 0) {
        return 0;
    }

    size_t out_count = 0;

    for (size_t i = 0; i < input_count; ++i) {
        const auto& raw = input[i];

        // Skip empty or blank SSIDs (e.g. hidden networks)
        if (raw.ssid[0] == '\0') {
            continue;
        }

        // Check if this SSID is already recorded
        int existing_idx = -1;
        for (size_t k = 0; k < out_count; ++k) {
            if (std::strncmp(output[k].ssid, raw.ssid, sizeof(raw.ssid)) == 0) {
                existing_idx = static_cast<int>(k);
                break;
            }
        }

        if (existing_idx >= 0) {
            // Keep the strongest signal
            if (raw.rssi > output[existing_idx].rssi) {
                output[existing_idx].rssi = raw.rssi;
                output[existing_idx].is_open = raw.is_open;
            }
        } else {
            if (out_count < max_output) {
                std::memcpy(&output[out_count], &raw, sizeof(DiscoveredNetwork));
                out_count++;
            } else {
                // Buffer is full: replace the element with the weakest RSSI if current is stronger
                size_t min_idx = 0;
                int8_t min_rssi = output[0].rssi;
                for (size_t k = 1; k < out_count; ++k) {
                    if (output[k].rssi < min_rssi) {
                        min_rssi = output[k].rssi;
                        min_idx = k;
                    }
                }
                if (raw.rssi > min_rssi) {
                    std::memcpy(&output[min_idx], &raw, sizeof(DiscoveredNetwork));
                }
            }
        }
    }

    // Sort descending by RSSI (strongest signal first)
    std::sort(output, output + out_count, [](const DiscoveredNetwork& a, const DiscoveredNetwork& b) {
        return a.rssi > b.rssi;
    });

    return out_count;
}

size_t serializeNetworksToJson(
    const DiscoveredNetwork* networks,
    size_t count,
    char* json_buf,
    size_t buf_size
) {
    if (!json_buf || buf_size < 3) {
        return 0;
    }

    size_t written = 0;
    json_buf[written++] = '[';

    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            if (written + 1 >= buf_size) return 0;
            json_buf[written++] = ',';
        }

        // Prefix: {"ssid":"
        const char prefix[] = "{\"ssid\":\"";
        constexpr size_t prefix_len = sizeof(prefix) - 1;
        if (written + prefix_len >= buf_size) return 0;
        std::memcpy(json_buf + written, prefix, prefix_len);
        written += prefix_len;

        // Escape and write SSID
        for (size_t c = 0; networks[i].ssid[c] != '\0'; ++c) {
            char ch = networks[i].ssid[c];
            if (ch == '\"' || ch == '\\') {
                if (written + 2 >= buf_size) return 0;
                json_buf[written++] = '\\';
                json_buf[written++] = ch;
            } else if (static_cast<unsigned char>(ch) < 0x20) {
                // Skip non-printable control characters
                continue;
            } else {
                if (written + 1 >= buf_size) return 0;
                json_buf[written++] = ch;
            }
        }

        // Suffix: ","rssi":-xx,"open":true|false}
        char tail[48];
        int tail_len = std::snprintf(tail, sizeof(tail), "\",\"rssi\":%d,\"open\":%s}",
                                     static_cast<int>(networks[i].rssi),
                                     networks[i].is_open ? "true" : "false");
        if (tail_len < 0 || written + static_cast<size_t>(tail_len) >= buf_size) {
            return 0;
        }
        std::memcpy(json_buf + written, tail, tail_len);
        written += tail_len;
    }

    if (written + 2 > buf_size) return 0;
    json_buf[written++] = ']';
    json_buf[written] = '\0';

    return written;
}
