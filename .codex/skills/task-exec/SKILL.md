---
name: task-exec
description: Execute exactly one Pending task from the repository execution matrix, implement and locally verify the change, then move it to QA Review without self-approving Done.
metadata:
  short-description: Execute one planned task and verify it locally
---

# `/task-exec`: Code Builder & Local Tester

Act as the senior implementation agent. Work on one planned task per invocation, preserve the project's conventions, run the appropriate local verification, and leave the task ready for an independent auditor.

## Invocation and task selection

```text
/task-exec [task_id]
```

- If `task_id` is supplied, locate that exact Task ID in the repository execution matrix.
- Otherwise select the first task, in document order, whose status is exactly `[ ] Pending`.
- Locate `PROGRESS.md` at the repository root first. If it is absent, inspect the referenced plan directory (for example `.ai/planning/<plan-name>/PROGRESS.md`) only when the repository's planning convention clearly identifies it; do not invent a matrix.
- If no Pending task exists, report that there is nothing to execute and stop without changing files.
- If an explicit ID is missing, ambiguous, or already `Done`, `In Progress`, or `QA Review`, stop and explain the status rather than silently selecting another task.

## Hard guardrails

1. **Single-task isolation:** implement only the selected task. Touch files outside its stated scope only for direct compile/test breakage caused by the change, and mention each such exception in the log.
2. **No self-approval:** never change a task to `[x] Done`. The maximum exit status is `[ ] QA Review`; only an independent audit may mark Done.
3. **Status discipline:** change the task to `[ ] In Progress` before implementation. Change it to `[ ] QA Review` only after verification succeeds. If blocked after genuine debugging, leave it `In Progress` and report the blocker; do not claim QA Review.
4. **Definition of Done:** avoid hardcoded secrets/configuration and unexplained magic values, keep reusable logic DRY, validate and type-check inputs, handle null/error paths, and avoid regressions or unintended side effects.
5. **Minimal diffs:** preserve UTF-8 text, existing formatting, unrelated behavior, and generated/vendor files. Inspect the diff before finishing.

## Required workflow

### 1. Lock and understand the task

1. Read the complete matrix and identify the task's Note, dependencies, target paths, and acceptance constraints.
2. Update only that task's status to `[ ] In Progress` immediately, preserving the matrix format.
3. Read the referenced `.ai/planning/<plan-name>/sprint_*.md` section and the relevant conventions in its `README.md`. If the matrix points to a different plan path, follow that exact path.
4. Inspect the existing implementation, tests, manifests, and neighboring conventions before editing.

### 2. Implement and test

1. Make the smallest production-ready implementation satisfying the task and its concrete acceptance criteria.
2. Add or update focused unit/integration tests for new behavior and boundary/error cases when the task changes executable behavior.
3. Detect the project stack from manifests and use the narrowest relevant checks first, then the project's normal suite:

| Stack | Verification (adapt to available scripts/config) |
|---|---|
| Go | `go vet ./...`, `golangci-lint run` if installed/configured, `go test -v ./...` |
| Node.js/TypeScript | package-manager lint script, `tsc --noEmit` when configured, package-manager test script |
| Python | `ruff check .` or configured linter, `pytest` |
| Flutter/Dart | `flutter analyze`, `flutter test` |
| PlatformIO/C++ | project-configured `pio test` and relevant build target; run repository scripts when the task requires them |
| SQL/migrations/scripts | the repository's rehearsal, shell, parser, or integration test commands documented by the task/plan |

Never claim a command passed if it was unavailable or skipped. On failure, inspect the output, fix the cause, and rerun the failed check plus relevant regression checks. Do not hand off known compiler, syntax, lint, or test failures.

### 3. Finalize for QA

1. Review `git diff` and `git status --short`; confirm no unrelated changes were introduced.
2. Update the selected task from `[ ] In Progress` to `[ ] QA Review` only when all required checks pass.
3. Prepend (newest first) a record to root `WALKTHROUGH_LOG.md`; create it if absent. Preserve existing entries exactly. Use:

```markdown
### [YYYY-MM-DD HH:mm] - Task <TASK_ID>: <task description>
* **Trạng thái:** `[ ] QA Review` (Chờ Auditor kiểm tra)
* **Files tác động:**
  - `[CREATED/MODIFIED]` path/to/file
* **Giải pháp kỹ thuật:** <1–2 câu tóm tắt>
* **Kết quả tự kiểm thử:** PASS (<commands/results>)

---
```

Use the current local time and retain Vietnamese Unicode. Include the actual verification commands/results, not invented test counts.

## Exit checklist and report

Before reporting, verify the task is `[ ] QA Review`, the newest log entry is at the top, and all required checks passed. Report briefly:

```text
Đã thực thi xong Task <TASK_ID>. Trạng thái: [ ] QA Review.
Tự kiểm tra cục bộ: THÀNH CÔNG (<summary of actual checks>).
Vui lòng chạy '/task-audit <TASK_ID>' để kiểm toán mã nguồn độc lập.
```

For a blocked task, report the exact failing command, root cause investigated, changes made, and why it remains `In Progress`.
