---
name: qa-loop
description: Orchestrate an explicit audit-and-remediation loop for repository tasks, routing both audits and fixes to gpt-5.5, until the selected plan scope is Done or a retry circuit breaker stops the run.
metadata:
  short-description: Orchestrate QA audit and fix loops
---

# `/qa-loop`: Sprint QA & Audit-Fix Orchestrator

Use this skill only when the user explicitly requests `/qa-loop`. It coordinates the existing `/task-audit` and `/task-fix` workflows through Codex CLI; it does not implement application changes itself.

## Invocation

```text
/qa-loop --plan <plan_name> [--sprint <sprint_id>] [--max-retries 3]
/qa-loop <plan_name> [--sprint <sprint_id>] [--max-retries 3]
```

- `--plan <plan_name>` or a bare positional `<plan_name>` token resolves to `.ai/planning/<plan_name>/PROGRESS.md`.
- `--sprint` optionally limits task selection to the named sprint/track.
- `--max-retries` defaults to `3` and limits remediation attempts for one task.

## Argument Resolution

Execute the following steps **in order, before any validation or state-machine logic**.

### Step 1 — Strip CLI Prefix (Mandatory)

The Codex interface may inject a skill prefix before the user's text, producing strings such as:

```
$qa-loop /qa-loop --plan aeroponics-lean
$qa-loop --plan aeroponics-lean
/qa-loop --plan aeroponics-lean
```

Before parsing any argument, strip every leading token that matches the pattern `$qa-loop` or `/qa-loop` (or any repetition thereof). Apply this unconditionally. The canonical input is the remainder after stripping.

### Step 2 — Extract `--plan` Flag (Highest Priority)

Scan the stripped input for either of:

- `--plan <name>` (space-separated)
- `--plan=<name>` (equals-separated)

If found, assign `PLAN=<name>`. This is the authoritative resolution path.

### Step 3 — Exact-Match Positional Fallback (UX Shortcut)

If and only if Step 2 found no `--plan` flag, scan the stripped input for any token that matches **exactly** (case-insensitive, no partial/fuzzy matching) the name of a directory present in `.ai/planning/`. The first such token is assigned as `PLAN`.

Log the resolution clearly:

```text
[INFO] Resolved plan from positional argument: <plan_name>
```

**Fuzzy matching is strictly prohibited.** If the token is a partial string or approximate match (e.g., `lean` when folders are `aeroponics-lean` and `aeroponics-enterprise`), treat it as unresolved and proceed to Step 4.

### Step 4 — Unresolved Plan (Stop Condition)

Only after Steps 1–3 have all failed to identify a plan name unambiguously, stop without mutation and report:

```text
[ERROR] Cannot resolve --plan. Provide --plan <name> or a positional argument matching an existing folder in .ai/planning/.
Available plans: <list directory names>
```

Do not stop earlier. The presence of multiple plan folders is not itself an ambiguity error; it only becomes one when no argument matches any folder exactly.

---

## Non-negotiable guardrails

1. Invoke `/task-audit` and `/task-fix` only with `--model gpt-5.5`. Do not silently substitute another model. If the model or CLI is unavailable, stop and report the blocked condition.
2. Never edit task checkboxes directly from the orchestrator. Only the delegated audit/fix skills may update `PROGRESS.md` state.
3. Run the repository's configured local syntax, lint, compile, and test pre-checks before every audit. Audit is allowed only when every applicable pre-check exits `0`; a failure must be handed to `/task-fix` only when there is an existing audit finding, otherwise stop and report the failed gate rather than inventing feedback.
4. Treat a rejected or failed audit as actionable only when its output contains a parseable task ID and actionable feedback. Unknown output, crash, timeout, or contradictory state is a hard stop.
5. Count consecutive rejected audits per task. When the count reaches `--max-retries` (default `3`), stop immediately with:

   ```text
   [CIRCUIT BREAKER TRIGGERED] Task <TASK_ID> thất bại qua 3 lần audit. Cần con người can thiệp.
   ```

6. Preserve unrelated working-tree changes. Do not stage, commit, rewrite logs, or mutate application files from this controller. Delegated skills must obey their own scope and state-transition rules.

---

## State-machine procedure

1. After Argument Resolution has produced a confirmed `PLAN` value, locate `.ai/planning/<PLAN>/PROGRESS.md`. Read its task format plus relevant `README.md`, sprint documents, and `WALKTHROUGH_LOG.md`.
2. Select the first in-scope task in document order whose status is `[ ] QA Review`. Also accept `[ ] In Progress` **only when** `WALKTHROUGH_LOG.md` contains `[AUDIT REJECTED] Task <TASK_ID>` as the most recent entry for that exact task ID, confirming it is the rejection state and not an unrelated work-in-progress. If every in-scope task is `[x] Done`, exit successfully. Do not select Pending or unrelated tasks.
3. Run the project's documented pre-check commands. Record exact commands and exit codes. Do not call audit after a failed gate.
4. Launch the audit subprocess:

   ```bash
   codex exec --model gpt-5.5 --dangerously-bypass-approvals-and-sandbox -- "/task-audit <TASK_ID>"
   ```

   Stream output when possible, then re-read `PROGRESS.md` to verify the delegated state transition.

5. On `[AUDIT APPROVED - LGTM]` and confirmed `[x] Done`, reset that task's retry counter and return to step 2.
6. On `[AUDIT REJECTED]`, retain the complete feedback, increment the task's consecutive retry counter, and if below the limit launch:

   ```bash
   codex exec --model gpt-5.5 --dangerously-bypass-approvals-and-sandbox -- "/task-fix <TASK_ID>" <<'EOF'
   <COMPLETE_AUDIT_FEEDBACK>
   EOF
   ```

   Require `/task-fix` to finish with `[ ] QA Review`; then return to step 3. Do not treat a fixer's claim as approval.

7. On any fix failure, missing handoff, failed post-fix pre-check, unknown parser result, or state mismatch, stop and report the task, command, state, and safe diagnostic output. Never force a checkbox transition.

---

## Completion report

When all selected tasks are Done, print:

```text
=====================================================
🎉 HOÀN TẤT VÒNG KIỂM TOÁN QA!
- Kế hoạch: <plan_name>
- Phạm vi: <Sprint / PROGRESS.md Toàn bộ>
- Tổng số Task đã chốt [x] Done: X/X
- Toàn bộ thay đổi đã được ghi vào WALKTHROUGH_LOG.md
=====================================================
```

The final count must be derived by re-reading the matrix, and the log claim must be verified rather than assumed.
