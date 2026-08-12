# Migration 001 rollback procedure

`001_production_domain_migration.sql` is intentionally additive and does not delete
legacy relay history. There is no safe generic SQL down migration: production rows
may be written after the migration and dropping the new domain would lose data.

Rollback is therefore **restore-from-snapshot only**:

1. Stop backend writers and record the migration timestamp.
2. Restore the verified pre-migration TimescaleDB snapshot into a fresh database.
3. Point the stack to the restored database only after schema and row-count checks.

The operational target is RPO equal to the snapshot interval and RTO determined by
the measured restore rehearsal. Do not use this procedure to silently delete legacy
tables; archive/retention decisions require an audited change record.
