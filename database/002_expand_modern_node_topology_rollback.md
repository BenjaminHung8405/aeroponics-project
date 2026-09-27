# Expand modern node topology rollback note

Migrations:

- `1726200010000-ExpandModernNodeTopology`
- `1726200009000-CreateControlSlots`

`001_production_domain_migration.sql` remains frozen at its original state, including its legacy node-range bounds. These later migrations are the forward expansion for production.

## Rollback rules

- Do not manually alter the live production schema after these migrations have run.
- `ExpandModernNodeTopology.down()` is intentionally gated: it throws if any table contains modern rows outside the legacy node set `4..7`.
- If a rollback to the legacy range is required, the only safe path is snapshot restore and re-migration.
- Control-slot assignments must be audited before rolling back: slots with `target_type = 'NODE' and target_id > 7` are incompatible with the legacy constraint and must be reassigned.

## Restore procedure

1. Stop backend writers and record the migration timestamp.
2. Restore the verified pre-expansion TimescaleDB snapshot into a fresh database.
3. After restore, confirm:
   - `group_node_assignments.node_id`, `sensor_calibrations.node_id`, `node_registry.node_id`, and event `node_id` columns are within the expected range for the chosen target firmware window.
   - `control_slots` does not contain targets outside the chosen target firmware window.
4. Point the stack to the restored database only after row-count and application-level validation checks pass.

The operational target is RPO equal to the snapshot interval and RTO determined by the measured restore rehearsal.
