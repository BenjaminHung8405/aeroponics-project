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

      // Verify that all default topics were subscribed with QoS 1
      DEFAULT_SUBSCRIBE_TOPICS.forEach((topic) => {
        expect(mockClient.subscribe).toHaveBeenCalledWith(
          topic,
          { qos: 1 },
          expect.any(Function),
        );
      });
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
        JSON.stringify(payload),
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
});
