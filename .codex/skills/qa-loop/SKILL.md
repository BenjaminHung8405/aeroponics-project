---
name: qa-loop
description: Orchestrate an explicit audit-and-remediation loop for repository tasks, routing audits to gpt-5.6-luna and fixes to gpt-5.6-terra until the selected plan scope is Done or a retry circuit breaker stops the run.
metadata:
  short-description: Orchestrate QA audit and fix loops
---

# `/qa-loop`: Sprint QA & Audit-Fix Orchestrator

Use this skill only when the user explicitly requests `/qa-loop`. It coordinates the existing `/task-audit` and `/task-fix` workflows through Codex CLI; it does not implement application changes itself.

## Invocation

```text
/qa-loop --plan <plan_name> [--sprint <sprint_id>] [--max-retries 3]
```

- `--plan` is required and resolves to `.ai/planning/<plan_name>/PROGRESS.md`.
- `--sprint` optionally limits task selection to the named sprint/track.
- `--max-retries` defaults to `3` and limits remediation attempts for one task.

## Non-negotiable guardrails

1. Invoke `/task-audit` only with `--model gpt-5.6-luna` and `/task-fix` only with `--model gpt-5.6-terra`. Do not silently substitute another model. If either model or the CLI is unavailable, stop and report the blocked condition.
2. Never edit task checkboxes directly from the orchestrator. Only the delegated audit/fix skills may update `PROGRESS.md` state.
3. Run the repository's configured local syntax, lint, compile, and test pre-checks before every audit. Audit is allowed only when every applicable pre-check exits `0`; a failure must be handed to `/task-fix` only when there is an existing audit finding, otherwise stop and report the failed gate rather than inventing feedback.
4. Treat a rejected or failed audit as actionable only when its output contains a parseable task ID and actionable feedback. Unknown output, crash, timeout, or contradictory state is a hard stop.
5. Count consecutive rejected audits per task. When the count reaches `--max-retries` (default `3`), stop immediately with:

   ```text
   [CIRCUIT BREAKER TRIGGERED] Task <TASK_ID> thất bại qua 3 lần audit. Cần con người can thiệp.
   ```

6. Preserve unrelated working-tree changes. Do not stage, commit, rewrite logs, or mutate application files from this controller. Delegated skills must obey their own scope and state-transition rules.

## State-machine procedure

1. Validate arguments, locate the plan matrix, and read its task format plus relevant `README.md`, sprint documents, and `WALKTHROUGH_LOG.md`. If the plan or scope cannot be resolved unambiguously, stop without mutation.
2. Select the first in-scope task in document order whose status is `[ ] QA Review`; also accept `[ ] In Progress` only when it is clearly the rejected state produced by the latest audit feedback. If every in-scope task is `[x] Done`, exit successfully. Do not select Pending or unrelated tasks.
3. Run the project's documented pre-check commands. Record exact commands and exit codes. Do not call audit after a failed gate.
4. Launch the audit subprocess with the exact routing:

   ```bash
   codex exec --model gpt-5.6-luna -- "/task-audit <TASK_ID>"
   ```

   Stream output when possible, then re-read `PROGRESS.md` to verify the delegated state transition.
5. On `[AUDIT APPROVED - LGTM]` and confirmed `[x] Done`, reset that task's retry counter and return to step 2.
6. On `[AUDIT REJECTED]`, retain the complete feedback, increment the task's consecutive retry counter, and if below the limit launch:

   ```bash
   codex exec --model gpt-5.6-terra -- "/task-fix <TASK_ID>" <<'EOF'
   <COMPLETE_AUDIT_FEEDBACK>
   EOF
   ```

   Require `/task-fix` to finish with `[ ] QA Review`; then return to step 3. Do not treat a fixer's claim as approval.
7. On any fix failure, missing handoff, failed post-fix pre-check, unknown parser result, or state mismatch, stop and report the task, command, state, and safe diagnostic output. Never force a checkbox transition.

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
