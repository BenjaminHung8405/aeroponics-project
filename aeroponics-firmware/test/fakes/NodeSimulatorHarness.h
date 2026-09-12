#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <map>
#include <algorithm>
#include "node_command_processor.h"
#include "fakes/FakeRfTransport.h"
#include "rf_frame_codec.h"
#include "config.h"

/**
 * @brief Deterministic Multi-Node Simulation Cluster & Test Harness.
 * 
 * Simulates up to 4 independent ATmega8 nodes (IDs 1..4) with realistic
 * RF link conditions, scheduled time slots, and hardware/hydraulic fault injection.
 */
class NodeSimulatorHarness {
public:
    struct LinkProfile {
        bool drop_rx = false;            // Drop incoming frames from gateway to node
        bool drop_tx = false;            // Drop outgoing frames from node to gateway
        bool corrupt_tx_crc = false;     // Corrupt CRC on return frames
        uint32_t ack_delay_ms = 0;       // Delay in delivering ACK to gateway
        float simulated_rssi_dbm = -65.0f;
    };

    struct DelayedFrame {
        uint32_t deliver_at_ms;
        std::vector<uint8_t> data;
    };

    class SimulatedActuatorDriver : public SimplePumpActuatorDriver {
    public:
        bool mismatch_enabled = false;

        void setPumpOutput(bool level) override {
            SimplePumpActuatorDriver::setPumpOutput(level);
            if (mismatch_enabled) {
                setSenseLevel(!level);
            }
        }
    };

    struct SimulatedNode {
        uint8_t node_id = 0;
        uint32_t boot_session_id = 100;
        SimulatedActuatorDriver driver;
        FakeRfTransport transport;
        NodeCommandProcessor processor;
        LinkProfile link;
        bool driver_mismatch = false;
    };

    explicit NodeSimulatorHarness(uint32_t seed = 0x4145524FUL)
        : _prng_seed(seed), _psk_len(16) {
        std::memset(_psk, 0, sizeof(_psk));
        _psk[0] = 0xA5;
    }

    ~NodeSimulatorHarness() = default;

    /**
     * @brief Initialize all 4 simulated nodes (IDs 1..4).
     */
    bool begin(const uint8_t* psk = nullptr, size_t psk_len = 0) {
        if (psk && psk_len > 0) {
            _psk_len = std::min(psk_len, sizeof(_psk));
            std::memcpy(_psk, psk, _psk_len);
        }

        for (uint8_t id = 1; id <= 4; ++id) {
            SimulatedNode& node = _nodes[id];
            node.node_id = id;
            node.boot_session_id = 100 + id;
            node.driver = SimulatedActuatorDriver();
            node.driver_mismatch = false;
            node.link = LinkProfile();
            node.transport.begin();

            if (!node.processor.begin(id, &node.transport, &node.driver,
                                     _psk, _psk_len, node.boot_session_id, nullptr)) {
                return false;
            }
        }
        return true;
    }

    // Accessors for nodes
    SimulatedNode& getNode(uint8_t node_id) {
        return _nodes[node_id];
    }

    NodeCommandProcessor& getProcessor(uint8_t node_id) {
        return _nodes[node_id].processor;
    }

    SimplePumpActuatorDriver& getDriver(uint8_t node_id) {
        return _nodes[node_id].driver;
    }

    LinkProfile& getLinkProfile(uint8_t node_id) {
        return _nodes[node_id].link;
    }

    // Hardware and Hydraulic Fault Injection
    void setDriverMismatch(uint8_t node_id, bool mismatch) {
        if (_nodes.find(node_id) != _nodes.end()) {
            _nodes[node_id].driver_mismatch = mismatch;
            _nodes[node_id].driver.mismatch_enabled = mismatch;
            if (mismatch) {
                // Invert driver sense so it contradicts output
                _nodes[node_id].driver.setSenseLevel(!_nodes[node_id].driver.getOutputLevel());
            } else {
                _nodes[node_id].driver.setSenseLevel(_nodes[node_id].driver.getOutputLevel());
            }
        }
    }

    void setHydraulicFlow(uint8_t node_id, uint16_t flow_lpm_x100, uint32_t volume_ml, uint32_t pulses) {
        if (_nodes.find(node_id) != _nodes.end()) {
            _nodes[node_id].driver.setFlowLpmX100(flow_lpm_x100);
            _nodes[node_id].driver.setDeliveredVolumeMl(volume_ml);
            _nodes[node_id].driver.setPulseCount(pulses);
        }
    }

    void setAcsCurrentMa(uint8_t node_id, uint16_t current_ma) {
        if (_nodes.find(node_id) != _nodes.end()) {
            _nodes[node_id].driver.setCurrentMa(current_ma);
        }
    }

    void rebootNode(uint8_t node_id, uint32_t current_time_ms) {
        if (_nodes.find(node_id) != _nodes.end()) {
            SimulatedNode& node = _nodes[node_id];
            node.boot_session_id += 10;
            node.driver.setPumpOutput(false);
            node.driver_mismatch = false;
            node.transport.flush();
            node.processor.begin(node_id, &node.transport, &node.driver,
                                 _psk, _psk_len, node.boot_session_id, nullptr);
            node.processor.service(current_time_ms);
        }
    }

    /**
     * @brief Process and route pending gateway transmissions to target nodes,
     * and route node responses back to the gateway.
     */
    void route(FakeRfTransport& gw_transport, uint32_t current_time_ms) {
        // Read and clear gateway TX buffer before delivering frames into RX buffer
        std::vector<uint8_t> outgoing_bytes = gw_transport.getTxBuffer();
        gw_transport.flush();

        // 1. Deliver any previously delayed frames that are now ready
        deliverPendingDelayedFrames(gw_transport, current_time_ms);

        if (outgoing_bytes.empty()) {
            stepNodes(current_time_ms);
            return;
        }

        // Process frames in outgoing buffer
        size_t offset = 0;
        while (offset + sizeof(RfHeader) <= outgoing_bytes.size()) {
            RfHeader header;
            if (!RfFrameCodec::decodeHeader(outgoing_bytes.data() + offset,
                                            outgoing_bytes.size() - offset,
                                            header)) {
                break;
            }

            size_t frame_total_len = sizeof(RfHeader) + header.payload_len + 16 + 2; // header + payload + hmac + crc
            if (offset + frame_total_len > outgoing_bytes.size()) {
                break;
            }

            const uint8_t* frame_ptr = outgoing_bytes.data() + offset;
            uint8_t target_id = header.target_node_id;

            if (target_id >= 1 && target_id <= 4) {
                deliverToNode(target_id, frame_ptr, frame_total_len, gw_transport, current_time_ms);
            } else if (target_id == 0) {
                // Broadcast to all 4 nodes
                for (uint8_t id = 1; id <= 4; ++id) {
                    deliverToNode(id, frame_ptr, frame_total_len, gw_transport, current_time_ms);
                }
            }

            offset += frame_total_len;
        }

        stepNodes(current_time_ms);
    }

    void stepNodes(uint32_t current_time_ms) {
        for (auto& pair : _nodes) {
            SimulatedNode& node = pair.second;
            node.processor.service(current_time_ms);

            // Re-apply driver mismatch if configured
            if (node.driver_mismatch) {
                node.driver.setSenseLevel(!node.driver.getOutputLevel());
            }
        }
    }

    /**
     * @brief Generate simulated staggered telemetry from a node directly to the gateway.
     */
    bool emitNodeTelemetry(uint8_t node_id, FakeRfTransport& gw_transport, uint32_t current_time_ms) {
        if (_nodes.find(node_id) == _nodes.end()) return false;
        SimulatedNode& node = _nodes[node_id];

        // Clear node's transport buffer first
        node.transport.flush();
        if (!node.processor.sendTelemetry(current_time_ms)) {
            return false;
        }

        const std::vector<uint8_t>& resp = node.transport.getTxBuffer();
        if (resp.empty() || node.link.drop_tx) {
            return false;
        }

        if (node.link.ack_delay_ms > 0) {
            _delayed_queue.push_back({current_time_ms + node.link.ack_delay_ms, resp});
        } else {
            gw_transport.injectRxData(resp.data(), resp.size());
        }
        return true;
    }

    // Deterministic pseudo-random generator
    uint32_t nextRandom() {
        _prng_seed = (_prng_seed * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
        return _prng_seed;
    }

    float nextRandomFloat() {
        return static_cast<float>(nextRandom()) / 2147483647.0f;
    }

private:
    void deliverToNode(uint8_t node_id, const uint8_t* frame_ptr, size_t frame_len,
                       FakeRfTransport& gw_transport, uint32_t current_time_ms) {
        SimulatedNode& node = _nodes[node_id];

        // Link drop simulation (gateway -> node)
        if (node.link.drop_rx) {
            return;
        }

        // Process incoming frame on node
        node.transport.flush();
        node.processor.processIncomingFrame(frame_ptr, frame_len, current_time_ms);

        // Update driver mismatch if active
        if (node.driver_mismatch) {
            node.driver.setSenseLevel(!node.driver.getOutputLevel());
        }

        // Check if node produced a response (e.g. COMMAND_ACK)
        const std::vector<uint8_t>& resp = node.transport.getTxBuffer();
        if (resp.empty()) {
            return;
        }

        // Link drop simulation (node -> gateway)
        if (node.link.drop_tx) {
            return;
        }

        std::vector<uint8_t> return_frame = resp;

        // CRC corruption simulation
        if (node.link.corrupt_tx_crc && return_frame.size() >= 2) {
            return_frame[return_frame.size() - 1] ^= 0xFF;
            return_frame[return_frame.size() - 2] ^= 0xFF;
        }

        // Delay delivery simulation
        if (node.link.ack_delay_ms > 0) {
            _delayed_queue.push_back({current_time_ms + node.link.ack_delay_ms, return_frame});
        } else {
            gw_transport.injectRxData(return_frame.data(), return_frame.size());
        }
    }

    void deliverPendingDelayedFrames(FakeRfTransport& gw_transport, uint32_t current_time_ms) {
        auto it = _delayed_queue.begin();
        while (it != _delayed_queue.end()) {
            if (current_time_ms >= it->deliver_at_ms) {
                gw_transport.injectRxData(it->data.data(), it->data.size());
                it = _delayed_queue.erase(it);
            } else {
                ++it;
            }
        }
    }

    uint32_t _prng_seed;
    uint8_t _psk[16];
    size_t _psk_len;
    std::map<uint8_t, SimulatedNode> _nodes;
    std::vector<DelayedFrame> _delayed_queue;
};
