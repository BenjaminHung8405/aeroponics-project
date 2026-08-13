-- Disposable representative Sprint 0 legacy state. Never run against production.
CREATE TABLE relay_profiles (relay_id SMALLINT PRIMARY KEY, spray_day_s INT NOT NULL);
CREATE TABLE relay_events (time TIMESTAMPTZ NOT NULL DEFAULT NOW(), relay_id SMALLINT NOT NULL, state TEXT NOT NULL);
CREATE TABLE sensor_readings (time TIMESTAMPTZ NOT NULL DEFAULT NOW(), sensor_id TEXT NOT NULL, value NUMERIC NOT NULL);
INSERT INTO relay_profiles VALUES (1, 30);
INSERT INTO relay_events (relay_id, state) VALUES (1, 'ON');
INSERT INTO sensor_readings (sensor_id, value) VALUES ('legacy-ph', 6.20);
