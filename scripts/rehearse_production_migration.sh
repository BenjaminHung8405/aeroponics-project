#!/usr/bin/env bash
set -euo pipefail

# Reproducible disposable migration rehearsal; requires Docker only.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
container="aero-migration-rehearsal-$$"
cleanup() { docker rm -f "$container" >/dev/null 2>&1 || true; }
trap cleanup EXIT

docker run -d --rm --name "$container" -e POSTGRES_PASSWORD=rehearsal -e POSTGRES_DB=aeroponics \
    timescale/timescaledb:latest-pg15 >/dev/null
until docker exec "$container" pg_isready -U postgres -d aeroponics >/dev/null 2>&1; do sleep 1; done
docker exec -i "$container" psql -v ON_ERROR_STOP=1 -U postgres -d aeroponics < "$ROOT/database/rehearsal/legacy_fixture.sql"
docker exec -i "$container" psql -v ON_ERROR_STOP=1 -U postgres -d aeroponics < "$ROOT/database/001_production_domain_migration.sql"

docker exec -i "$container" psql -v ON_ERROR_STOP=1 -U postgres -d aeroponics <<'SQL'
INSERT INTO seasons (name, status) VALUES ('Legacy rehearsal', 'ACTIVE');
DO $$
DECLARE table_count INT; hypertable_count INT;
BEGIN
  IF (SELECT spray_day_s FROM relay_profiles WHERE relay_id = 1) <> 30
     OR (SELECT state FROM relay_events ORDER BY time LIMIT 1) <> 'ON'
     OR (SELECT value FROM sensor_readings WHERE sensor_id = 'legacy-ph') <> 6.20 THEN
    RAISE EXCEPTION 'legacy data was changed';
  END IF;
  SELECT count(*) INTO table_count FROM information_schema.tables
    WHERE table_schema = 'public' AND table_name IN ('devices','seasons','treatments','treatment_versions','timer_groups','group_treatment_assignments','group_node_assignments','sensor_calibrations','node_registry','device_status','tuya_measurement_sessions');
  IF table_count <> 11 THEN RAISE EXCEPTION 'expected 11 production regular tables, got %', table_count; END IF;
  SELECT count(*) INTO hypertable_count FROM timescaledb_information.hypertables
    WHERE hypertable_name IN ('measurement_readings','pump_commands','pump_state_events','pump_feedback_events','flow_events');
  IF hypertable_count <> 5 THEN RAISE EXCEPTION 'expected 5 hypertables, got %', hypertable_count; END IF;
END $$;

INSERT INTO treatments (name) VALUES ('Rehearsal');
INSERT INTO treatment_versions (treatment_id, version_num, spray_day_s, cooldown_day_s, spray_night_s, cooldown_night_s)
  VALUES (1, 1, 30, 300, 30, 300);
-- Partial uniqueness: two current assignments for one season/node must fail.
INSERT INTO group_node_assignments (group_id, node_id, season_id) VALUES (1, 1, 1);
DO $$ BEGIN
  BEGIN
    INSERT INTO group_node_assignments (group_id, node_id, season_id) VALUES (2, 1, 1);
    RAISE EXCEPTION 'partial unique index did not reject duplicate';
  EXCEPTION WHEN unique_violation THEN NULL;
  END;
END $$;
-- Season guard: a second active season makes un-attributed event history unsafe.
INSERT INTO seasons (name, status) VALUES ('Ambiguous', 'ACTIVE');
ALTER TABLE flow_events ALTER COLUMN season_id DROP NOT NULL;
INSERT INTO flow_events (season_id, node_id) VALUES (NULL, 1);
DO $$
BEGIN
  IF (SELECT count(*) FROM seasons WHERE status = 'ACTIVE') <> 2
     OR NOT EXISTS (SELECT 1 FROM flow_events WHERE season_id IS NULL) THEN
    RAISE EXCEPTION 'season-attribution safety fixture invalid';
  END IF;
END $$;
SQL

echo 'PASS disposable production migration rehearsal: legacy preserved, 11 tables, 5 hypertables, indexes and season guard verified'
