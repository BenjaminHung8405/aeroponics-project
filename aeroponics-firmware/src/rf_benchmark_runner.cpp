#include "rf_benchmark_runner.h"
#include <math.h>
#include <algorithm>

static void sortFloatArray(float* arr, uint32_t n) {
    if (!arr || n <= 1) return;
    std::sort(arr, arr + n);
}

RfLatencyBreakdown RfBenchmarkRunner::calculateBreakdown(
    uint8_t req_frame_len,
    uint8_t resp_frame_len,
    uint32_t uart_baud,
    uint32_t air_baud,
    float node_delay_ms,
    float flow_delay_ms
) {
    RfLatencyBreakdown b;
    if (uart_baud == 0) uart_baud = 9600;
    if (air_baud == 0) air_baud = 9600;

    // 8-N-1 UART framing: 10 bits per byte transmitted
    b.uart_tx_ms = (static_cast<float>(req_frame_len) * 10.0f * 1000.0f) / static_cast<float>(uart_baud);
    
    // RF PHY framing: 4 preamble bytes + 2 sync bytes + frame bytes (8 bits per byte)
    b.airtime_fwd_ms = (static_cast<float>(req_frame_len + 6) * 8.0f * 1000.0f) / static_cast<float>(air_baud);
    
    // Node internal execution (HMAC verify, state machine, GPIO assert)
    b.node_proc_ms = node_delay_ms;
    
    // RF PHY response airtime
    b.airtime_rev_ms = (static_cast<float>(resp_frame_len + 6) * 8.0f * 1000.0f) / static_cast<float>(air_baud);
    
    // Gateway UART reception
    b.uart_rx_ms = (static_cast<float>(resp_frame_len) * 10.0f * 1000.0f) / static_cast<float>(uart_baud);
    
    // Flow sensor hydraulic transit & confirmation time
    b.flow_confirm_ms = flow_delay_ms;
    
    // Network Round Trip Time
    b.round_trip_ms = b.uart_tx_ms + b.airtime_fwd_ms + b.node_proc_ms + b.airtime_rev_ms + b.uart_rx_ms;
    
    // Total latency until flow confirmed
    b.total_with_flow_ms = b.round_trip_ms + b.flow_confirm_ms;

    return b;
}

bool RfBenchmarkRunner::calculatePercentiles(
    float* samples,
    uint32_t count,
    float& p50,
    float& p90,
    float& p95,
    float& p99
) {
    if (!samples || count == 0) {
        p50 = p90 = p95 = p99 = 0.0f;
        return false;
    }

    sortFloatArray(samples, count);

    auto getPercentile = [samples, count](float p) -> float {
        if (count == 1) return samples[0];
        float rank = (p / 100.0f) * static_cast<float>(count - 1);
        uint32_t lower_idx = static_cast<uint32_t>(floorf(rank));
        uint32_t upper_idx = static_cast<uint32_t>(ceilf(rank));
        if (lower_idx >= count) lower_idx = count - 1;
        if (upper_idx >= count) upper_idx = count - 1;
        float weight = rank - static_cast<float>(lower_idx);
        return samples[lower_idx] + weight * (samples[upper_idx] - samples[lower_idx]);
    };

    p50 = getPercentile(50.0f);
    p90 = getPercentile(90.0f);
    p95 = getPercentile(95.0f);
    p99 = getPercentile(99.0f);

    return true;
}

bool RfBenchmarkRunner::calculateStatistics(
    float* samples,
    uint32_t sample_count,
    uint32_t loss_count,
    uint32_t retry_count,
    float avg_rssi,
    uint8_t avg_lqi,
    float reconnect_ms,
    RfBenchmarkStats& out_stats
) {
    if (!samples || sample_count == 0) {
        return false;
    }

    out_stats.sample_count = sample_count;
    out_stats.loss_count = loss_count;
    out_stats.retry_count = retry_count;
    out_stats.success_count = (sample_count >= loss_count) ? (sample_count - loss_count) : 0;
    out_stats.packet_loss_rate_pct = (static_cast<float>(loss_count) / static_cast<float>(sample_count)) * 100.0f;
    out_stats.avg_rssi_dbm = avg_rssi;
    out_stats.avg_lqi = avg_lqi;
    out_stats.reconnect_time_ms = reconnect_ms;

    float sum = 0.0f;
    float min_val = samples[0];
    float max_val = samples[0];

    for (uint32_t i = 0; i < sample_count; ++i) {
        float val = samples[i];
        sum += val;
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
    }

    float mean = sum / static_cast<float>(sample_count);
    out_stats.min_latency_ms = min_val;
    out_stats.max_latency_ms = max_val;
    out_stats.mean_latency_ms = mean;

    float variance_sum = 0.0f;
    for (uint32_t i = 0; i < sample_count; ++i) {
        float diff = samples[i] - mean;
        variance_sum += diff * diff;
    }
    out_stats.std_dev_ms = sqrtf(variance_sum / static_cast<float>(sample_count));

    calculatePercentiles(samples, sample_count,
        out_stats.p50_latency_ms,
        out_stats.p90_latency_ms,
        out_stats.p95_latency_ms,
        out_stats.p99_latency_ms);

    return true;
}

float RfBenchmarkRunner::estimateRssi(RfBenchmarkEnvironment env, int8_t tx_power_dbm) {
    // Relative to configured TX power (nominal +14 dBm / 25 mW)
    float base_offset = static_cast<float>(tx_power_dbm - 14);

    switch (env) {
        case ENV_LOS_CLEAR_10M:
            return -55.0f + base_offset;
        case ENV_LOS_CLEAR_30M:
            return -68.0f + base_offset;
        case ENV_LOS_CLEAR_50M:
            return -75.0f + base_offset;
        case ENV_LOS_CLEAR_100M:
            return -84.0f + base_offset;
        case ENV_WET_FOLIAGE_CANOPY:
            return -89.0f + base_offset; // ~18 dB canopy attenuation
        case ENV_CONCRETE_METAL_OBSTACLE:
            return -92.0f + base_offset;
        case ENV_INDUCTIVE_EMI_BURST:
            return -62.0f + base_offset; // Close proximity, but high RF noise floor
        default:
            return -70.0f + base_offset;
    }
}

float RfBenchmarkRunner::estimatePdr(RfBenchmarkEnvironment env, RfModulationMode mod) {
    if (mod == RF_MOD_LORA_E32) {
        // LoRa Chirp Spread Spectrum resilience
        switch (env) {
            case ENV_LOS_CLEAR_10M:
            case ENV_LOS_CLEAR_30M:
            case ENV_LOS_CLEAR_50M:
            case ENV_LOS_CLEAR_100M:
                return 1.000f; // 100% PDR
            case ENV_WET_FOLIAGE_CANOPY:
                return 0.990f; // 99.0% PDR
            case ENV_CONCRETE_METAL_OBSTACLE:
                return 0.970f; // 97.0% PDR
            case ENV_INDUCTIVE_EMI_BURST:
                return 0.995f; // 99.5% PDR
            default:
                return 0.980f;
        }
    } else {
        // FSK HC-12 (No hardware FEC)
        switch (env) {
            case ENV_LOS_CLEAR_10M:
                return 1.000f;
            case ENV_LOS_CLEAR_30M:
                return 0.995f;
            case ENV_LOS_CLEAR_50M:
                return 0.980f;
            case ENV_LOS_CLEAR_100M:
                return 0.940f;
            case ENV_WET_FOLIAGE_CANOPY:
                return 0.910f; // Multi-path fading through wet leaves
            case ENV_CONCRETE_METAL_OBSTACLE:
                return 0.880f;
            case ENV_INDUCTIVE_EMI_BURST:
                return 0.960f; // Inductive arcing corrupts occasional packets
            default:
                return 0.950f;
        }
    }
}

void RfBenchmarkRunner::runDeterministicTrialSuite(
    const RfRadioConfig& config,
    RfBenchmarkEnvironment env,
    RfModulationMode mod,
    uint32_t num_samples,
    float base_node_delay_ms,
    float flow_delay_ms,
    RfBenchmarkStats& out_stats,
    float* out_samples,
    uint32_t max_samples
) {
    if (num_samples == 0) return;

    // Standard frame lengths from RF_PROTOCOL:
    // SET_PUMP: 17B header + 9B payload + 16B HMAC + 2B CRC = 44 bytes
    // COMMAND_ACK: 17B header + 8B payload + 16B HMAC + 2B CRC = 43 bytes
    uint8_t req_len = 44;
    uint8_t resp_len = 43;

    RfLatencyBreakdown nominal = calculateBreakdown(
        req_len, resp_len, config.uart_baud_bps, config.air_baud_bps,
        base_node_delay_ms, flow_delay_ms
    );

    float base_rtt = nominal.round_trip_ms;
    float pdr = estimatePdr(env, mod);
    float rssi = estimateRssi(env, config.tx_power_dbm);

    // Estimate LQI mapping: -100 dBm (0) to -40 dBm (255)
    int lqi_val = static_cast<int>((rssi + 100.0f) * (255.0f / 60.0f));
    if (lqi_val < 0) lqi_val = 0;
    if (lqi_val > 255) lqi_val = 255;
    uint8_t lqi = static_cast<uint8_t>(lqi_val);

    uint32_t loss_count = 0;
    uint32_t retry_count = 0;

    // Buffer for samples
    float local_samples[256];
    float* sample_buf = (out_samples && max_samples >= num_samples) ? out_samples : local_samples;
    uint32_t effective_samples = (num_samples <= 256) ? num_samples : 256;

    for (uint32_t i = 0; i < effective_samples; ++i) {
        // Deterministic pseudo-random variation based on trial index
        float jitter = (static_cast<float>((i * 17 + 31) % 100) / 100.0f - 0.5f) * 2.5f; // +/- 1.25 ms
        float trial_rtt = base_rtt + jitter;

        // Determine if this trial experienced a packet loss / retry based on PDR
        float rand_factor = static_cast<float>((i * 73 + 19) % 1000) / 1000.0f;
        if (rand_factor > pdr) {
            // First attempt lost, triggered bounded retry (e.g. + retry backoff timeout ~350ms)
            retry_count++;
            trial_rtt += 350.0f + base_rtt; // Retried successfully
            
            // Severe channel condition secondary drop
            if (rand_factor > (pdr + (1.0f - pdr) * 0.85f)) {
                loss_count++;
            }
        }

        sample_buf[i] = trial_rtt;
    }

    float reconnect_time = (mod == RF_MOD_LORA_E32) ? 1450.0f : 850.0f;

    calculateStatistics(
        sample_buf,
        effective_samples,
        loss_count,
        retry_count,
        rssi,
        lqi,
        reconnect_time,
        out_stats
    );
}
