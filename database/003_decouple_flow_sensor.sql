-- =============================================================================
-- Migration 003: Decouple Flow Sensor from Pump Commands (Open-Loop Actuation)
-- =============================================================================
-- Allows pump commands (action = 'ON') to execute without requiring an ACTIVE
-- sensor calibration when flow meters are absent (open-loop / timer mode).
-- If a node is explicitly marked CALIBRATED, active calibration integrity is preserved.

CREATE OR REPLACE FUNCTION assert_pump_on_calibration() RETURNS TRIGGER AS $$
BEGIN
    -- Decoupled / Open-Loop Flow Mode:
    -- If a node is marked CALIBRATED, verify that its active calibration is valid and ACTIVE.
    -- If the node is UNCALIBRATED (open-loop mode), allow pump actuation without blocking.
    IF NEW.action = 'ON' AND EXISTS (
        SELECT 1 FROM node_registry node 
        WHERE node.node_id = NEW.node_id AND node.calibration_status = 'CALIBRATED'
    ) THEN
        IF NOT EXISTS (
            SELECT 1 FROM node_registry node
            JOIN sensor_calibrations calibration
              ON calibration.id = node.active_sensor_calibration_id
            WHERE node.node_id = NEW.node_id
              AND node.calibration_status = 'CALIBRATED'
              AND calibration.node_id = NEW.node_id
              AND calibration.sensor_serial = node.sensor_serial
              AND calibration.status = 'ACTIVE'
        ) THEN
            RAISE EXCEPTION 'pump ON for node % requires an ACTIVE sensor calibration', NEW.node_id;
        END IF;
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_pump_commands_require_active_calibration ON pump_commands;
CREATE TRIGGER trg_pump_commands_require_active_calibration
BEFORE INSERT OR UPDATE OF action, node_id ON pump_commands
FOR EACH ROW EXECUTE FUNCTION assert_pump_on_calibration();
