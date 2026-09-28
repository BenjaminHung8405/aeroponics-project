-- =============================================================================
-- Migration 004: Seed Hardware Topology Group Node Assignments
-- =============================================================================
-- Pre-assigns the 15 modern nodes (0x01..0x0F) into the 4 fixed IIoT RF timer groups
-- per hardware wire specification: groupID = 0x10 | (nodeID & 0x0C):
--  - Group 1 ($10): Nodes [1, 2, 3]
--  - Group 2 ($14): Nodes [4, 5, 6, 7]
--  - Group 3 ($18): Nodes [8, 9, 10, 11] (0x08, 0x09, 0x0A, 0x0B)
--  - Group 4 ($1C): Nodes [12, 13, 14, 15] (0x0C, 0x0D, 0x0E, 0x0F)
--
-- Synchronizes node_registry, cached_group_id, timer_groups status, and binds to active season.

DO $$
DECLARE
  v_season_id INT;
  v_treatment_version_id INT;
BEGIN
  -- 1. Resolve or create active season
  SELECT id INTO v_season_id FROM seasons WHERE status = 'ACTIVE' ORDER BY id DESC LIMIT 1;
  IF v_season_id IS NULL THEN
    SELECT id INTO v_season_id FROM seasons ORDER BY id DESC LIMIT 1;
  END IF;
  IF v_season_id IS NULL THEN
    INSERT INTO seasons (name, status, started_at)
    VALUES ('Default Season', 'ACTIVE', NOW())
    RETURNING id INTO v_season_id;
  END IF;

  -- 2. Ensure all 4 Timer Groups exist and are ACTIVE with hardware RF labeling
  INSERT INTO timer_groups (group_id, name, status, updated_at)
  VALUES
    (1, 'Group 1 ($10)', 'ACTIVE', NOW()),
    (2, 'Group 2 ($14)', 'ACTIVE', NOW()),
    (3, 'Group 3 ($18)', 'ACTIVE', NOW()),
    (4, 'Group 4 ($1C)', 'ACTIVE', NOW())
  ON CONFLICT (group_id) DO UPDATE SET
    name = EXCLUDED.name,
    status = 'ACTIVE',
    updated_at = NOW();

  -- 3. Ensure nodes 1..15 exist in node_registry with cached_group_id synchronized
  INSERT INTO node_registry (node_id, display_name, cached_group_id, calibration_status)
  VALUES
    (1, 'Node 01', 1, 'UNCALIBRATED'),
    (2, 'Node 02', 1, 'UNCALIBRATED'),
    (3, 'Node 03', 1, 'UNCALIBRATED'),
    (4, 'Node 04', 2, 'UNCALIBRATED'),
    (5, 'Node 05', 2, 'UNCALIBRATED'),
    (6, 'Node 06', 2, 'UNCALIBRATED'),
    (7, 'Node 07', 2, 'UNCALIBRATED'),
    (8, 'Node 08', 3, 'UNCALIBRATED'),
    (9, 'Node 09', 3, 'UNCALIBRATED'),
    (10, 'Node 10 (0A)', 3, 'UNCALIBRATED'),
    (11, 'Node 11 (0B)', 3, 'UNCALIBRATED'),
    (12, 'Node 12 (0C)', 4, 'UNCALIBRATED'),
    (13, 'Node 13 (0D)', 4, 'UNCALIBRATED'),
    (14, 'Node 14 (0E)', 4, 'UNCALIBRATED'),
    (15, 'Node 15 (0F)', 4, 'UNCALIBRATED')
  ON CONFLICT (node_id) DO UPDATE SET
    cached_group_id = EXCLUDED.cached_group_id,
    updated_at = NOW();

  -- 4. Close any currently active node assignments in this season
  UPDATE group_node_assignments
  SET active = FALSE, effective_to = NOW()
  WHERE season_id = v_season_id AND active = TRUE AND effective_to IS NULL;

  -- 5. Seed fixed hardware group assignments
  INSERT INTO group_node_assignments (group_id, node_id, season_id, effective_from, effective_to, active)
  VALUES
    -- Group 1 ($10): Nodes [1, 2, 3]
    (1, 1, v_season_id, NOW(), NULL, TRUE),
    (1, 2, v_season_id, NOW(), NULL, TRUE),
    (1, 3, v_season_id, NOW(), NULL, TRUE),
    -- Group 2 ($14): Nodes [4, 5, 6, 7]
    (2, 4, v_season_id, NOW(), NULL, TRUE),
    (2, 5, v_season_id, NOW(), NULL, TRUE),
    (2, 6, v_season_id, NOW(), NULL, TRUE),
    (2, 7, v_season_id, NOW(), NULL, TRUE),
    -- Group 3 ($18): Nodes [8, 9, 10, 11] (0x08..0x0B)
    (3, 8, v_season_id, NOW(), NULL, TRUE),
    (3, 9, v_season_id, NOW(), NULL, TRUE),
    (3, 10, v_season_id, NOW(), NULL, TRUE),
    (3, 11, v_season_id, NOW(), NULL, TRUE),
    -- Group 4 ($1C): Nodes [12, 13, 14, 15] (0x0C..0x0F)
    (4, 12, v_season_id, NOW(), NULL, TRUE),
    (4, 13, v_season_id, NOW(), NULL, TRUE),
    (4, 14, v_season_id, NOW(), NULL, TRUE),
    (4, 15, v_season_id, NOW(), NULL, TRUE);

  -- 6. If a published treatment version exists, bind it to all groups if not already assigned
  SELECT id INTO v_treatment_version_id FROM treatment_versions WHERE status = 'PUBLISHED' ORDER BY id ASC LIMIT 1;
  IF v_treatment_version_id IS NOT NULL THEN
    UPDATE group_treatment_assignments
    SET active = FALSE, unassigned_at = NOW()
    WHERE season_id = v_season_id AND active = TRUE AND unassigned_at IS NULL;

    INSERT INTO group_treatment_assignments (group_id, treatment_version_id, season_id, assigned_at, unassigned_at, active)
    VALUES
      (1, v_treatment_version_id, v_season_id, NOW(), NULL, TRUE),
      (2, v_treatment_version_id, v_season_id, NOW(), NULL, TRUE),
      (3, v_treatment_version_id, v_season_id, NOW(), NULL, TRUE),
      (4, v_treatment_version_id, v_season_id, NOW(), NULL, TRUE);
  END IF;

END $$;
