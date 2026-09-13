import {
  Injectable,
  Logger,
  OnModuleDestroy,
  OnModuleInit,
} from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { EventEmitter2 } from '@nestjs/event-emitter';
import * as mqtt from 'mqtt';
import { DEFAULT_SUBSCRIBE_TOPICS, MQTT_EVENTS } from './mqtt.constants';

export interface ParsedMqttMessage {
  topic: string;
  payload: any;
  deviceId?: string;
  nodeId?: number;
  commandId?: string;
  receivedAt: Date;
}

@Injectable()
export class MqttService implements OnModuleInit, OnModuleDestroy {
  private readonly logger = new Logger(MqttService.name);
  private client: mqtt.MqttClient | null = null;
  private connected = false;

  constructor(
    private readonly configService: ConfigService,
    private readonly eventEmitter: EventEmitter2,
  ) {}

  async onModuleInit(): Promise<void> {
    const isTest = this.configService.get<string>('NODE_ENV') === 'test';
    const host = this.configService.get<string>('MQTT_HOST');

    // In test environment without explicit host or when disabled, avoid network auto-connect
    if (isTest && !host) {
      this.logger.log('MQTT client auto-connect skipped in test environment.');
      return;
    }

    this.connect();
  }

  connect(): void {
    if (this.client) {
      return;
    }

    const host = this.configService.get<string>('MQTT_HOST', 'localhost');
    const port = this.configService.get<number>('MQTT_PORT', 1883);
    const username = this.configService.get<string>('MQTT_USERNAME');
    const password = this.configService.get<string>('MQTT_PASSWORD');
    const clientId =
      this.configService.get<string>('MQTT_CLIENT_ID', 'aeroponics_backend_service') +
      '_' +
      Math.random().toString(16).substring(2, 8);

    const brokerUrl = `mqtt://${host}:${port}`;
    this.logger.log(`Connecting to MQTT broker at ${brokerUrl} with clientId: ${clientId}`);

    try {
      this.client = mqtt.connect(brokerUrl, {
        clientId,
        username,
        password,
        clean: true,
        reconnectPeriod: 2000,
        connectTimeout: 30000,
      });

      this.setupClientListeners();
    } catch (err: any) {
      this.logger.error(`Failed to initialize MQTT connection: ${err?.message || err}`);
    }
  }

  /**
   * Directly inject a client (useful for unit/integration testing with mock broker)
   */
  setClient(client: mqtt.MqttClient): void {
    this.client = client;
    this.setupClientListeners();
  }

  private setupClientListeners(): void {
    if (!this.client) return;

    this.client.on('connect', () => {
      this.connected = true;
      this.logger.log('MQTT Client connected successfully.');
      this.eventEmitter.emit(MQTT_EVENTS.CONNECTION_CHANGED, { connected: true });
      this.subscribeDefaultTopics();
    });

    this.client.on('reconnect', () => {
      this.logger.warn('MQTT Client reconnecting...');
    });

    this.client.on('close', () => {
      if (this.connected) {
        this.connected = false;
        this.logger.warn('MQTT Client disconnected.');
        this.eventEmitter.emit(MQTT_EVENTS.CONNECTION_CHANGED, { connected: false });
      }
    });

    this.client.on('error', (err) => {
      this.logger.error(`MQTT Client error: ${err.message}`, err.stack);
    });

    this.client.on('message', (topic: string, messageBuffer: Buffer) => {
      this.handleMessage(topic, messageBuffer);
    });
  }

  private subscribeDefaultTopics(): void {
    if (!this.client) return;

    DEFAULT_SUBSCRIBE_TOPICS.forEach((topic) => {
      this.client!.subscribe(topic, { qos: 1 }, (err) => {
        if (err) {
          this.logger.error(`Failed to subscribe to topic ${topic}: ${err.message}`);
        } else {
          this.logger.log(`Subscribed to topic: ${topic}`);
        }
      });
    });
  }

  /**
   * Resilient, zero-crash onMessage handler.
   * HARD RULE S3-MQTT-05: Catch all exceptions, never crash event loop on malformed payloads.
   */
  public handleMessage(topic: string, messageBuffer: Buffer): void {
    try {
      const payloadString = messageBuffer.toString('utf-8');
      let parsedPayload: any;

      try {
        parsedPayload = JSON.parse(payloadString);
      } catch (jsonErr: any) {
        this.logger.warn(
          `Malformed JSON payload received on topic "${topic}": ${jsonErr.message}. Raw payload: "${payloadString}"`,
        );
        this.eventEmitter.emit(MQTT_EVENTS.PARSE_ERROR, {
          topic,
          rawPayload: payloadString,
          error: jsonErr.message,
        });
        return; // Gracefully drop invalid frame without crashing
      }

      this.routeMessage(topic, parsedPayload);
    } catch (err: any) {
      // Outermost safeguard against catastrophic unhandled exceptions in decoding/routing
      this.logger.error(
        `Critical exception during MQTT message handling on topic "${topic}": ${err?.message || err}`,
        err?.stack,
      );
    }
  }

  private routeMessage(topic: string, payload: any): void {
    const receivedAt = new Date();

    // 1. Gateway Status: aeroponics/device/{deviceId}/status
    const statusMatch = topic.match(/^aeroponics\/device\/([^/]+)\/status$/);
    if (statusMatch) {
      const deviceId = statusMatch[1];
      this.eventEmitter.emit(MQTT_EVENTS.DEVICE_STATUS, {
        topic,
        deviceId,
        payload,
        receivedAt,
      });
      return;
    }

    // 2. Gateway Telemetry: aeroponics/device/{deviceId}/telemetry
    const telemetryMatch = topic.match(/^aeroponics\/device\/([^/]+)\/telemetry$/);
    if (telemetryMatch) {
      const deviceId = telemetryMatch[1];
      const nodeId = typeof payload.node_id === 'number' ? payload.node_id : undefined;
      this.eventEmitter.emit(MQTT_EVENTS.TELEMETRY, {
        topic,
        deviceId,
        nodeId,
        payload,
        receivedAt,
      });
      return;
    }

    // 3. Command ACK: aeroponics/device/{deviceId}/command/{commandId}/ack
    const cmdAckMatch = topic.match(/^aeroponics\/device\/([^/]+)\/command\/([^/]+)\/ack$/);
    if (cmdAckMatch) {
      const deviceId = cmdAckMatch[1];
      const commandId = cmdAckMatch[2];
      this.eventEmitter.emit(MQTT_EVENTS.COMMAND_ACK, {
        topic,
        deviceId,
        commandId,
        payload,
        receivedAt,
      });
      return;
    }

    // 4. Node Snapshot: aeroponics/telemetry/node/{nodeId}/snapshot
    const nodeSnapshotMatch = topic.match(/^aeroponics\/telemetry\/node\/([^/]+)\/snapshot$/);
    if (nodeSnapshotMatch) {
      const nodeId = parseInt(nodeSnapshotMatch[1], 10);
      this.eventEmitter.emit(MQTT_EVENTS.TELEMETRY, {
        topic,
        nodeId,
        payload,
        receivedAt,
      });
      return;
    }

    // 5. Node Event: aeroponics/telemetry/node/{nodeId}/event
    const nodeEventMatch = topic.match(/^aeroponics\/telemetry\/node\/([^/]+)\/event$/);
    if (nodeEventMatch) {
      const nodeId = parseInt(nodeEventMatch[1], 10);
      this.eventEmitter.emit(MQTT_EVENTS.NODE_EVENT, {
        topic,
        nodeId,
        payload,
        receivedAt,
      });
      return;
    }

    // 6. Generic Command ACK: aeroponics/ack/{commandId}
    const genericAckMatch = topic.match(/^aeroponics\/ack\/([^/]+)$/);
    if (genericAckMatch) {
      const commandId = genericAckMatch[1];
      this.eventEmitter.emit(MQTT_EVENTS.COMMAND_ACK, {
        topic,
        commandId,
        payload,
        receivedAt,
      });
      return;
    }

    // 7. Safety Audit: aeroponics/device/{deviceId}/safety/audit
    const safetyMatch = topic.match(/^aeroponics\/device\/([^/]+)\/safety\/audit$/);
    if (safetyMatch) {
      const deviceId = safetyMatch[1];
      this.eventEmitter.emit(MQTT_EVENTS.SAFETY_AUDIT, {
        topic,
        deviceId,
        payload,
        receivedAt,
      });
      return;
    }

    // Fallback: emit generic message
    this.logger.debug(`Unhandled topic pattern received: ${topic}`);
  }

  public async publish(
    topic: string,
    message: any,
    options: mqtt.IClientPublishOptions = { qos: 1 },
  ): Promise<void> {
    if (!this.client || !this.connected) {
      throw new Error(`Cannot publish message: MQTT client is not connected.`);
    }

    return new Promise((resolve, reject) => {
      const payloadString =
        typeof message === 'string' ? message : JSON.stringify(message);

      this.client!.publish(topic, payloadString, options, (err) => {
        if (err) {
          this.logger.error(`Failed to publish to topic "${topic}": ${err.message}`);
          return reject(err);
        }
        resolve();
      });
    });
  }

  public isConnected(): boolean {
    return this.connected;
  }

  onModuleDestroy(): void {
    if (this.client) {
      this.logger.log('Closing MQTT client connection gracefully...');
      this.client.end(true);
      this.client = null;
      this.connected = false;
    }
  }
}
