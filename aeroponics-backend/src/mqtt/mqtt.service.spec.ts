import 'reflect-metadata';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { ConfigService } from '@nestjs/config';
import { MqttService } from './mqtt.service';
import { DEFAULT_SUBSCRIBE_TOPICS, MQTT_EVENTS } from './mqtt.constants';
import { EventEmitter } from 'events';

class MockMqttClient extends EventEmitter {
  public subscribe = jest.fn((topic, options, callback) => {
    if (callback) callback(null);
  });
  public publish = jest.fn((topic, message, options, callback) => {
    if (callback) callback(null);
  });
  public end = jest.fn();
}

describe('MqttService', () => {
  let service: MqttService;
  let eventEmitter: EventEmitter2;
  let mockClient: MockMqttClient;

  const mockConfigService = {
    get: jest.fn((key: string, defaultValue?: any) => {
      if (key === 'NODE_ENV') return 'test';
      if (key === 'MQTT_HOST') return 'localhost';
      if (key === 'MQTT_PORT') return 1883;
      if (key === 'MQTT_USERNAME') return 'aero_user';
      if (key === 'MQTT_PASSWORD') return 'aero_pass';
      if (key === 'MQTT_CLIENT_ID') return 'aero_backend_test';
      return defaultValue;
    }),
  } as unknown as ConfigService;

  beforeEach(() => {
    eventEmitter = new EventEmitter2();
    jest.spyOn(eventEmitter, 'emit');

    service = new MqttService(mockConfigService, eventEmitter);
    mockClient = new MockMqttClient();
    service.setClient(mockClient as any);
  });

  afterEach(() => {
    service.onModuleDestroy();
  });

  describe('Lifecycle & Subscriptions (S3-A3)', () => {
    it('should subscribe to default topic contracts upon connection', () => {
      mockClient.emit('connect');

      expect(service.isConnected()).toBe(true);
      expect(eventEmitter.emit).toHaveBeenCalledWith(MQTT_EVENTS.CONNECTION_CHANGED, {
        connected: true,
      });

      // Verify that default topics were subscribed in a batch with QoS 1
      expect(mockClient.subscribe).toHaveBeenCalledWith(
        [...DEFAULT_SUBSCRIBE_TOPICS],
        { qos: 1 },
        expect.any(Function),
      );
    });

    it('should emit disconnect event when client closes', () => {
      mockClient.emit('connect');
      expect(service.isConnected()).toBe(true);

      mockClient.emit('close');
      expect(service.isConnected()).toBe(false);
      expect(eventEmitter.emit).toHaveBeenCalledWith(MQTT_EVENTS.CONNECTION_CHANGED, {
        connected: false,
      });
    });

    it('should subscribe to every v1 node topic required by the production namespace', () => {
      expect(DEFAULT_SUBSCRIBE_TOPICS).toEqual(
        expect.arrayContaining([
          'aeroponics/v1/node/+/ack',
          'aeroponics/v1/node/+/telemetry',
          'aeroponics/v1/node/+/event',
          'aeroponics/v1/node/+/flow',
          'aeroponics/v1/node/+/fault',
        ]),
      );
    });
  });

  describe('Zero-Crash Exception Safety (S3-MQTT-05 / S3-A3 Requirement 1)', () => {
    it('should catch malformed JSON payloads without crashing or throwing', () => {
      const malformedPayload = Buffer.from('NOT_VALID_JSON{abc:123');

      expect(() => {
        service.handleMessage('aeroponics/device/esp32_gw_01/telemetry', malformedPayload);
      }).not.toThrow();

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        MQTT_EVENTS.PARSE_ERROR,
        expect.objectContaining({
          topic: 'aeroponics/device/esp32_gw_01/telemetry',
          rawPayload: 'NOT_VALID_JSON{abc:123',
        }),
      );
    });

    it('should catch completely empty or corrupted binary buffers gracefully', () => {
      const corruptedBuffer = Buffer.from([0xff, 0xfe, 0x00, 0x12]);

      expect(() => {
        service.handleMessage('aeroponics/device/esp32_gw_01/telemetry', corruptedBuffer);
      }).not.toThrow();

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        MQTT_EVENTS.PARSE_ERROR,
        expect.anything(),
      );
    });
  });

  describe('Topic Contract Routing (Sprint 2 & 3 Contracts)', () => {
    it('should route gateway telemetry correctly', () => {
      const telemetry = {
        node_id: 4,
        pump_state: 1,
        flow_lpm: 1.85,
        delivered_volume_ml: 450,
      };
      const buffer = Buffer.from(JSON.stringify(telemetry));

      service.handleMessage('aeroponics/device/esp32_gateway_01/telemetry', buffer);

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        MQTT_EVENTS.TELEMETRY,
        expect.objectContaining({
          topic: 'aeroponics/device/esp32_gateway_01/telemetry',
          deviceId: 'esp32_gateway_01',
          nodeId: 4,
          payload: telemetry,
        }),
      );
    });

    it('should route gateway status/LWT correctly', () => {
      const statusPayload = {
        status: 'online',
        uptime_s: 86400,
        rssi_dbm: -65,
      };
      const buffer = Buffer.from(JSON.stringify(statusPayload));

      service.handleMessage('aeroponics/device/esp32_gateway_01/status', buffer);

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        MQTT_EVENTS.DEVICE_STATUS,
        expect.objectContaining({
          deviceId: 'esp32_gateway_01',
          payload: statusPayload,
        }),
      );
    });

    it('should route command ACK correctly', () => {
      const ackPayload = {
        command_id: 'cmd_manual_001',
        status: 'ACCEPTED',
        node_id: 5,
      };
      const buffer = Buffer.from(JSON.stringify(ackPayload));

      service.handleMessage(
        'aeroponics/device/esp32_gateway_01/command/cmd_manual_001/ack',
        buffer,
      );

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        MQTT_EVENTS.COMMAND_ACK,
        expect.objectContaining({
          deviceId: 'esp32_gateway_01',
          commandId: 'cmd_manual_001',
          payload: ackPayload,
        }),
      );
    });

    it('should route v1 node ACK topic aeroponics/v1/node/{nodeId}/ack to COMMAND_ACK event', () => {
      const ackPayload = {
        command_id: 'rf-cmd-1',
        status: 'ACCEPTED',
        node_id: 4,
        schema_version: '1.0',
      };
      const buffer = Buffer.from(JSON.stringify(ackPayload));

      service.handleMessage('aeroponics/v1/node/4/ack', buffer);

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        MQTT_EVENTS.COMMAND_ACK,
        expect.objectContaining({
          topic: 'aeroponics/v1/node/4/ack',
          nodeId: 4,
          payload: ackPayload,
          schema_version: '1.0',
        }),
      );
    });

    it('should route v1 node telemetry topic to NODE_TELEMETRY event', () => {
      const telemetryPayload = {
        node_id: 4,
        flow_rate_lpm: 2.4,
        delivered_volume_ml: 450,
        schema_version: '1.0',
      };
      const buffer = Buffer.from(JSON.stringify(telemetryPayload));

      service.handleMessage('aeroponics/v1/node/4/telemetry', buffer);

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        MQTT_EVENTS.NODE_TELEMETRY,
        expect.objectContaining({
          topic: 'aeroponics/v1/node/4/telemetry',
          nodeId: 4,
          payload: telemetryPayload,
          schema_version: '1.0',
        }),
      );
    });

    it('should route v1 gateway heartbeat to GATEWAY_HEARTBEAT event', () => {
      const heartbeatPayload = { uptime_s: 3600, firmware_version: '2.0.1' };
      const buffer = Buffer.from(JSON.stringify(heartbeatPayload));

      service.handleMessage('aeroponics/v1/gateway/esp32_gw_01/heartbeat', buffer);

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        MQTT_EVENTS.GATEWAY_HEARTBEAT,
        expect.objectContaining({
          topic: 'aeroponics/v1/gateway/esp32_gw_01/heartbeat',
          gatewayId: 'esp32_gw_01',
          payload: heartbeatPayload,
        }),
      );
    });

    it('should discard v1 message with non-integer node ID without emitting v1 events', () => {
      const buffer = Buffer.from(JSON.stringify({ data: 'test' }));

      service.handleMessage('aeroponics/v1/node/abc/ack', buffer);

      // Invalid nodeId segment must fail parseInt check and be dropped silently.
      expect(eventEmitter.emit).not.toHaveBeenCalledWith(
        MQTT_EVENTS.COMMAND_ACK,
        expect.anything(),
      );
    });

    it('should route node snapshot topic correctly', () => {
      const snapshotPayload = {
        pulse_count: 120,
        flow_lpm: 2.1,
      };
      const buffer = Buffer.from(JSON.stringify(snapshotPayload));

      service.handleMessage('aeroponics/telemetry/node/6/snapshot', buffer);

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        MQTT_EVENTS.TELEMETRY,
        expect.objectContaining({
          nodeId: 6,
          payload: snapshotPayload,
        }),
      );
    });
  });

  describe('Publishing Operations', () => {
    it('should throw error when publishing while disconnected', async () => {
      await expect(
        service.publish('aeroponics/device/esp32_01/command/override', { test: true }),
      ).rejects.toThrow('Cannot publish message: MQTT client is not connected.');
    });

    it('should publish message successfully when connected', async () => {
      mockClient.emit('connect');
      const payload = { action: 'ON', run_lease_ms: 15000 };

      await service.publish('aeroponics/device/esp32_01/command/override', payload);

      expect(mockClient.publish).toHaveBeenCalledWith(
        'aeroponics/device/esp32_01/command/override',
        JSON.stringify({
          ...payload,
          sender: 'backend',
          origin: 'backend',
        }),
        { qos: 1, retain: false },
        expect.any(Function),
      );
    });

    it('should enforce retain=true for status/LWT topics', async () => {
      mockClient.emit('connect');
      await service.publish('aeroponics/device/gw/status', { status: 'online' });

      expect(mockClient.publish).toHaveBeenCalledWith(
        'aeroponics/device/gw/status',
        expect.any(String),
        { qos: 1, retain: true },
        expect.any(Function),
      );
    });

    it('should enforce retain=false for heartbeat topics', async () => {
      mockClient.emit('connect');
      await service.publish('aeroponics/v1/gateway/gw/heartbeat', {
        status: 'online',
      });

      expect(mockClient.publish).toHaveBeenCalledWith(
        'aeroponics/v1/gateway/gw/heartbeat',
        expect.any(String),
        { qos: 1, retain: false },
        expect.any(Function),
      );
    });

    it('should prioritize status classification over transactional suffixes', async () => {
      mockClient.emit('connect');
      await service.publish('aeroponics/device/gw/status/ack', {
        status: 'online',
      });

      expect(mockClient.publish).toHaveBeenCalledWith(
        'aeroponics/device/gw/status/ack',
        expect.any(String),
        { qos: 1, retain: true },
        expect.any(Function),
      );
    });

    it('should use exact path-segment regex matching instead of substring matching', async () => {
      mockClient.emit('connect');
      await service.publish('aeroponics/v1/node/4/ack_event', {
        command_id: 'rf-cmd-1',
      });

      expect(mockClient.publish).toHaveBeenCalledWith(
        'aeroponics/v1/node/4/ack_event',
        expect.any(String),
        { qos: 1, retain: false },
        expect.any(Function),
      );
    });

    it('should enforce retain=false for v1 transactional topics', async () => {
      mockClient.emit('connect');
      await service.publish('aeroponics/v1/node/4/ack', { status: 'OK' });

      expect(mockClient.publish).toHaveBeenCalledWith(
        'aeroponics/v1/node/4/ack',
        expect.any(String),
        { qos: 1, retain: false },
        expect.any(Function),
      );
    });

    it('should enforce retain=false for v1 event topics', async () => {
      mockClient.emit('connect');
      await service.publish('aeroponics/v1/node/4/event', {
        schema_version: '1.0',
        event: 'RF_ACKED',
      });

      expect(mockClient.publish).toHaveBeenCalledWith(
        'aeroponics/v1/node/4/event',
        expect.any(String),
        { qos: 1, retain: false },
        expect.any(Function),
      );
    });

    it('should default retain to false when topic does not match any classification', async () => {
      mockClient.emit('connect');
      await service.publish('aeroponics/random/unknown/topic', { ok: true });

      expect(mockClient.publish).toHaveBeenCalledWith(
        'aeroponics/random/unknown/topic',
        expect.any(String),
        { qos: 1, retain: false },
        expect.any(Function),
      );
    });
  });

  describe('Self-Message Suppression & Message Storm Prevention', () => {
    it('Tier 1 Guard: should drop outbound command and config topics immediately', () => {
      const outboundTopics = [
        'aeroponics/command/node/1/override',
        'aeroponics/treatment/schedule',
        'aeroponics/device/esp32_gw/command/node/1/override',
        'aeroponics/device/esp32_gw/config/control_slots',
        'aeroponics/v1/node/1/command',
      ];

      for (const topic of outboundTopics) {
        expect(service.isOutboundTopic(topic)).toBe(true);

        // Sending message on outbound topic should be silently dropped without emitting any events
        service.handleMessage(topic, Buffer.from(JSON.stringify({ some: 'data' })));
      }

      expect(eventEmitter.emit).not.toHaveBeenCalledWith(
        expect.stringMatching(/^mqtt\./),
        expect.anything(),
      );
    });

    it('Tier 1 Guard: should NOT drop inbound ACKs that contain /command/ but end with /ack', () => {
      const inboundAckTopic = 'aeroponics/device/esp32_gw/command/cmd-123/ack';
      expect(service.isOutboundTopic(inboundAckTopic)).toBe(false);

      service.handleMessage(
        inboundAckTopic,
        Buffer.from(JSON.stringify({ status: 'SUCCESS' })),
      );

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        MQTT_EVENTS.COMMAND_ACK,
        expect.objectContaining({
          commandId: 'cmd-123',
          deviceId: 'esp32_gw',
        }),
      );
    });

    it('Tier 2 Guard: should drop payloads where sender === "backend" (echo loop prevention)', () => {
      const echoPayload = Buffer.from(
        JSON.stringify({
          sender: 'backend',
          node_id: 1,
          temp_c: 24.5,
        }),
      );

      service.handleMessage('aeroponics/device/esp32_gw/telemetry', echoPayload);

      // Should be dropped by Tier 2 guard, no TELEMETRY event emitted
      expect(eventEmitter.emit).not.toHaveBeenCalledWith(
        MQTT_EVENTS.TELEMETRY,
        expect.anything(),
      );
    });

    it('Tier 2 Guard: should drop payloads where source or origin is "backend"', () => {
      service.handleMessage(
        'aeroponics/device/esp32_gw/telemetry',
        Buffer.from(JSON.stringify({ source: 'backend', node_id: 1 })),
      );
      service.handleMessage(
        'aeroponics/device/esp32_gw/telemetry',
        Buffer.from(JSON.stringify({ origin: 'backend', node_id: 1 })),
      );

      expect(eventEmitter.emit).not.toHaveBeenCalledWith(
        MQTT_EVENTS.TELEMETRY,
        expect.anything(),
      );
    });

    it('Tier 2 Guard: should drop payloads where clientId matches this backend instance', () => {
      // Set a test clientId
      (service as any).clientId = 'aeroponics_backend_test_client_id';

      service.handleMessage(
        'aeroponics/device/esp32_gw/telemetry',
        Buffer.from(
          JSON.stringify({
            clientId: 'aeroponics_backend_test_client_id',
            node_id: 1,
          }),
        ),
      );

      expect(eventEmitter.emit).not.toHaveBeenCalledWith(
        MQTT_EVENTS.TELEMETRY,
        expect.anything(),
      );
    });

    it('should route genuine device messages with no backend sender metadata', () => {
      const devicePayload = Buffer.from(
        JSON.stringify({
          node_id: 2,
          humidity: 85,
        }),
      );

      service.handleMessage('aeroponics/device/esp32_gw/telemetry', devicePayload);

      expect(eventEmitter.emit).toHaveBeenCalledWith(
        MQTT_EVENTS.TELEMETRY,
        expect.objectContaining({
          deviceId: 'esp32_gw',
          nodeId: 2,
          payload: { node_id: 2, humidity: 85 },
        }),
      );
    });

    it('should clean up old listeners when setClient is called multiple times', () => {
      const client1 = new MockMqttClient();
      const removeSpy = jest.spyOn(client1, 'removeAllListeners');

      service.setClient(client1 as any);
      expect(removeSpy).toHaveBeenCalledWith('connect');
      expect(removeSpy).toHaveBeenCalledWith('message');
    });

    it('should generate dynamic clientId containing process.pid and random hex', () => {
      const generated = service.generateClientId('test_prefix');
      expect(generated).toMatch(new RegExp(`^test_prefix_${process.pid}_[0-9a-f]{6}$`));

      const defaultGenerated = service.generateClientId();
      expect(defaultGenerated).toContain(`_${process.pid}_`);
    });
  });
});
