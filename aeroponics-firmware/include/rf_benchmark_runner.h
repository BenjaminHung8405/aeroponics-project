#ifndef RF_BENCHMARK_RUNNER_H
#define RF_BENCHMARK_RUNNER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief RF Channel Environmental Conditions for Benchmark Testing
 */
enum RfBenchmarkEnvironment {
    ENV_LOS_CLEAR_10M = 0,
    ENV_LOS_CLEAR_30M = 1,
    ENV_LOS_CLEAR_50M = 2,
    ENV_LOS_CLEAR_100M = 3,
    ENV_WET_FOLIAGE_CANOPY = 4,
    ENV_CONCRETE_METAL_OBSTACLE = 5,
    ENV_INDUCTIVE_EMI_BURST = 6
};

/**
 * @brief RF Modulation Schemes
 */
enum RfModulationMode {
    RF_MOD_FSK_HC12 = 0,
    RF_MOD_LORA_E32 = 1
};

/**
 * @brief Radio Physical Configuration Profile
 */
struct RfRadioConfig {
    uint32_t frequency_hz;      // Center frequency (e.g. 433175000 = 433.175 MHz)
    uint8_t channel;            // Channel index (e.g. 1)
    uint32_t air_baud_bps;      // Over-the-air PHY data rate in bps (e.g. 9600)
    uint32_t uart_baud_bps;     // Host-to-module UART baud rate (e.g. 9600 or 115200)
    int8_t tx_power_dbm;        // Configured transmit power in dBm (e.g. 14 dBm / 25 mW)
    const char* antenna_type;   // Antenna model description
};

/**
 * @brief Detailed Microsecond-Level Latency Breakdown
 */
struct RfLatencyBreakdown {
    float uart_tx_ms;           // Gateway MCU -> RF Module UART serialization time
    float airtime_fwd_ms;       // 433 MHz Airtime transmission (Gateway -> Node)
    float node_proc_ms;         // Node MCU decode, HMAC verification, relay trigger
    float airtime_rev_ms;       // 433 MHz Airtime response (Node -> Gateway)
    float uart_rx_ms;           // RF Module -> Gateway MCU UART buffer read
    float flow_confirm_ms;      // Fluid hydraulic transit & sensor pulse confirmation
    float round_trip_ms;        // Network RTT: uart_tx + air_fwd + node_proc + air_rev + uart_rx
    float total_with_flow_ms;   // End-to-end command success latency: RTT + flow_confirm
};

/**
 * @brief Benchmark Statistical Aggregation Record
 */
struct RfBenchmarkStats {
    uint32_t sample_count;           // Total packets / trials transmitted
    uint32_t success_count;          // Packets acknowledged within timeout
    uint32_t loss_count;             // Packets dropped / timed out
    uint32_t retry_count;            // Retransmissions required
    float packet_loss_rate_pct;      // (loss_count / sample_count) * 100.0%
    float min_latency_ms;            // Minimum observed latency
    float max_latency_ms;            // Maximum observed latency
    float mean_latency_ms;           // Arithmetic mean latency
    float std_dev_ms;                // Standard deviation
    float p50_latency_ms;            // 50th percentile (median) latency
    float p90_latency_ms;            // 90th percentile latency
    float p95_latency_ms;            // 95th percentile latency
    float p99_latency_ms;            // 99th percentile latency
    float avg_rssi_dbm;              // Average RSSI
    uint8_t avg_lqi;                 // Link Quality Indicator (0-255)
    float reconnect_time_ms;         // Power-cycle recovery & session resync latency
};

/**
 * @brief RF Benchmark Runner & Analytical Evaluation Engine
 */
class RfBenchmarkRunner {
public:
    /**
     * @brief Calculate theoretical time-domain breakdown of an RF transaction.
     * 
     * @param req_frame_len Full request frame size in bytes (header + payload + hmac + crc)
     * @param resp_frame_len Full response frame size in bytes
     * @param uart_baud UART baud rate in bps (8-N-1: 10 bits per byte)
     * @param air_baud Over-the-air RF baud rate in bps (including preamble + sync)
     * @param node_delay_ms Node internal processing delay in ms
     * @param flow_delay_ms Hydraulic flow propagation and verification delay in ms
     */
    static RfLatencyBreakdown calculateBreakdown(
        uint8_t req_frame_len,
        uint8_t resp_frame_len,
        uint32_t uart_baud,
        uint32_t air_baud,
        float node_delay_ms,
        float flow_delay_ms
    );

    /**
     * @brief Compute statistical percentiles (p50, p90, p95, p99) from an array of latency samples.
     * Note: Expects an array that will be sorted or pre-sorted.
     */
    static bool calculatePercentiles(
        float* samples,
        uint32_t count,
        float& p50,
        float& p90,
        float& p95,
        float& p99
    );

    /**
     * @brief Compute full statistics (mean, variance, std dev, min, max, percentiles).
     */
    static bool calculateStatistics(
        float* samples,
        uint32_t sample_count,
        uint32_t loss_count,
        uint32_t retry_count,
        float avg_rssi,
        uint8_t avg_lqi,
        float reconnect_ms,
        RfBenchmarkStats& out_stats
    );

    /**
     * @brief Estimate expected RSSI in dBm based on environment path loss model (Log-Distance Model).
     */
    static float estimateRssi(RfBenchmarkEnvironment env, int8_t tx_power_dbm);

    /**
     * @brief Estimate Packet Delivery Ratio (0.0 to 1.0) under given channel conditions.
     */
    static float estimatePdr(RfBenchmarkEnvironment env, RfModulationMode mod);

    /**
     * @brief Execute a deterministic empirical benchmark suite simulation for bench test validation.
     */
    static void runDeterministicTrialSuite(
        const RfRadioConfig& config,
        RfBenchmarkEnvironment env,
        RfModulationMode mod,
        uint32_t num_samples,
        float base_node_delay_ms,
        float flow_delay_ms,
        RfBenchmarkStats& out_stats,
        float* out_samples = nullptr,
        uint32_t max_samples = 0
    );
};

#endif // RF_BENCHMARK_RUNNER_H
