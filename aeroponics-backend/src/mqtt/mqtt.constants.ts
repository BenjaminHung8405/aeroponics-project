export const MQTT_TOPICS = {
  DEVICE_STATUS: 'aeroponics/device/+/status',
  DEVICE_TELEMETRY: 'aeroponics/device/+/telemetry',
  DEVICE_COMMAND_ACK: 'aeroponics/device/+/command/+/ack',
  DEVICE_SAFETY_AUDIT: 'aeroponics/device/+/safety/audit',
  NODE_SNAPSHOT: 'aeroponics/telemetry/node/+/snapshot',
  NODE_EVENT: 'aeroponics/telemetry/node/+/event',
  COMMAND_ACK: 'aeroponics/ack/+',
} as const;

export const DEFAULT_SUBSCRIBE_TOPICS = [
  MQTT_TOPICS.DEVICE_STATUS,
  MQTT_TOPICS.DEVICE_TELEMETRY,
  MQTT_TOPICS.DEVICE_COMMAND_ACK,
  MQTT_TOPICS.DEVICE_SAFETY_AUDIT,
  MQTT_TOPICS.NODE_SNAPSHOT,
  MQTT_TOPICS.NODE_EVENT,
  MQTT_TOPICS.COMMAND_ACK,
];

export const MQTT_EVENTS = {
  DEVICE_STATUS: 'mqtt.device.status',
  TELEMETRY: 'mqtt.telemetry',
  COMMAND_ACK: 'mqtt.command.ack',
  SAFETY_AUDIT: 'mqtt.safety.audit',
  NODE_EVENT: 'mqtt.node.event',
  PARSE_ERROR: 'mqtt.parse.error',
  CONNECTION_CHANGED: 'mqtt.connection.changed',
} as const;
