export const MQTT_TOPICS = {
  DEVICE_STATUS: 'aeroponics/device/+/status',
  DEVICE_TELEMETRY: 'aeroponics/device/+/telemetry',
  DEVICE_COMMAND_ACK: 'aeroponics/device/+/command/+/ack',
  DEVICE_SAFETY_AUDIT: 'aeroponics/device/+/safety/audit',
  NODE_SNAPSHOT: 'aeroponics/telemetry/node/+/snapshot',
  DEVICE_NODE_SNAPSHOT: 'aeroponics/device/+/telemetry/node/+/snapshot',
  NODE_EVENT: 'aeroponics/telemetry/node/+/event',
  COMMAND_ACK: 'aeroponics/ack/+',

  // Sprint 3 Track I topics
  GATEWAY_HEARTBEAT: 'aeroponics/gateway/+/heartbeat',
  NODE_TELEMETRY: 'aeroponics/node/+/telemetry',
  NODE_FLOW: 'aeroponics/node/+/flow',
  NODE_ACK: 'aeroponics/node/+/ack',
  NODE_FAULT: 'aeroponics/node/+/fault',
  GATEWAY_SCAN_RESULTS: 'aeroponics/device/+/telemetry/gateway/scan_results',
  DEVICE_COMMAND_ACK_DIRECT: 'aeroponics/device/+/ack/+',
} as const;

export const DEFAULT_SUBSCRIBE_TOPICS = [
  MQTT_TOPICS.DEVICE_STATUS,
  MQTT_TOPICS.DEVICE_TELEMETRY,
  MQTT_TOPICS.DEVICE_COMMAND_ACK,
  MQTT_TOPICS.DEVICE_COMMAND_ACK_DIRECT,
  MQTT_TOPICS.DEVICE_SAFETY_AUDIT,
  MQTT_TOPICS.NODE_SNAPSHOT,
  MQTT_TOPICS.DEVICE_NODE_SNAPSHOT,
  MQTT_TOPICS.NODE_EVENT,
  MQTT_TOPICS.COMMAND_ACK,
  MQTT_TOPICS.GATEWAY_HEARTBEAT,
  MQTT_TOPICS.NODE_TELEMETRY,
  MQTT_TOPICS.NODE_FLOW,
  MQTT_TOPICS.NODE_ACK,
  MQTT_TOPICS.NODE_FAULT,
  MQTT_TOPICS.GATEWAY_SCAN_RESULTS,
];

export const MQTT_PUBLISH_TEMPLATES = {
  NODE_PUMP: (gatewayId: string, nodeId: number) =>
    `aeroponics/gateway/${gatewayId}/command/node/${nodeId}/pump`,
  GROUP_CONFIG: (gatewayId: string, groupId: number) =>
    `aeroponics/gateway/${gatewayId}/command/group/${groupId}/config`,
  NODE_OVERRIDE: (nodeId: number) =>
    `aeroponics/command/node/${nodeId}/override`,
  GATEWAY_SCAN: (deviceId: string) =>
    `aeroponics/device/${deviceId}/command/gateway/scan_rf`,
  GATEWAY_CLAIM: (deviceId: string) =>
    `aeroponics/device/${deviceId}/command/gateway/claim_node`,
} as const;

export const MQTT_EVENTS = {
  DEVICE_STATUS: 'mqtt.device.status',
  TELEMETRY: 'mqtt.telemetry',
  COMMAND_ACK: 'mqtt.command.ack',
  SAFETY_AUDIT: 'mqtt.safety.audit',
  NODE_EVENT: 'mqtt.node.event',
  NODE_SNAPSHOT: 'mqtt.node.snapshot',
  PARSE_ERROR: 'mqtt.parse.error',
  CONNECTION_CHANGED: 'mqtt.connection.changed',

  // Sprint 3 Track I routed events
  GATEWAY_HEARTBEAT: 'mqtt.gateway.heartbeat',
  NODE_TELEMETRY: 'mqtt.node.telemetry',
  NODE_FLOW: 'mqtt.node.flow',
  NODE_ACK: 'mqtt.node.ack',
  NODE_FAULT: 'mqtt.node.fault',
  GATEWAY_SCAN_RESULTS: 'mqtt.gateway.scan_results',
} as const;
