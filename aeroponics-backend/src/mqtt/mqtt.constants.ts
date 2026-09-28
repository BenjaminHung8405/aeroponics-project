export const MQTT_TOPICS = {
  // V1 Production namespace (firmware publishes here — aeroponics/v1/node/{id}/...)
  V1_NODE_ACK: 'aeroponics/v1/node/+/ack',
  V1_NODE_TELEMETRY: 'aeroponics/v1/node/+/telemetry',
  V1_NODE_FLOW: 'aeroponics/v1/node/+/flow',
  V1_NODE_EVENT: 'aeroponics/v1/node/+/event',
  V1_NODE_FAULT: 'aeroponics/v1/node/+/fault',
  V1_GATEWAY_HEARTBEAT: 'aeroponics/v1/gateway/+/heartbeat',
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

export const MQTT_RETAIN_POLICY = {
  STATUS_LWT: true,
  TRANSACTIONAL: false,
  HEARTBEAT: false,
} as const;

export const MQTT_V1_PUBLISH = {
  NODE_COMMAND: (nodeId: number) => `aeroponics/v1/node/${nodeId}/command`,
  NODE_ACK: (nodeId: number) => `aeroponics/v1/node/${nodeId}/ack`,
  NODE_TELEMETRY: (nodeId: number) => `aeroponics/v1/node/${nodeId}/telemetry`,
  NODE_EVENT: (nodeId: number) => `aeroponics/v1/node/${nodeId}/event`,
  GATEWAY_HEARTBEAT: (gatewayId: string) =>
    `aeroponics/v1/gateway/${gatewayId}/heartbeat`,
} as const;

export const DEFAULT_SUBSCRIBE_TOPICS = [
  MQTT_TOPICS.V1_NODE_ACK,
  MQTT_TOPICS.V1_NODE_TELEMETRY,
  MQTT_TOPICS.V1_NODE_FLOW,
  MQTT_TOPICS.V1_NODE_EVENT,
  MQTT_TOPICS.V1_NODE_FAULT,
  MQTT_TOPICS.V1_GATEWAY_HEARTBEAT,
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
  // Authoritative backend time-set downlink (DS1307 migration).
  GATEWAY_CLOCK: (deviceId: string) =>
    `aeroponics/device/${deviceId}/command/config/clock`,
  // Downlink treatment schedule profile to gateway
  GATEWAY_TREATMENT_CONFIG: (deviceId: string) =>
    `aeroponics/device/${deviceId}/command/config/treatment`,
  // Downlink node-to-group assignment to gateway
  GATEWAY_ASSIGNMENT_CONFIG: (deviceId: string) =>
    `aeroponics/device/${deviceId}/command/config/assignment`,
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
  COMMAND_ACCEPTED: 'mqtt.command.accepted',
} as const;
