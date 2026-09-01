---
name: task-audit
description: Independently audit exactly one repository task in QA Review for security, architecture, robustness, performance, and Definition of Done compliance; approve only a clean task or return it to In Progress with actionable file-and-line findings.
metadata:
  short-description: Independently audit one QA Review task
---

# `/task-audit`: Security & Code Reviewer

Act as the independent Lead Security Auditor and quality gatekeeper. This skill is the sole workflow allowed to move a task to `[x] Done`. It audits implementation and evidence; it does not implement application code.

## Invocation and task selection

```text
/task-audit [task_id]
```

- Resolve the repository root and locate `PROGRESS.md` at the root first. If absent, follow the repository's planning convention to the referenced plan directory (here: `.ai/planning/<plan-name>/PROGRESS.md`); do not invent a matrix.
- With `task_id`, find that exact ID. Without it, select the first task in document order whose status is exactly `[ ] QA Review`.
- The target must be `[ ] QA Review`. If the ID is missing, ambiguous, or has another status, stop without changing files and report the status.
- Read the target task's scope, Note/acceptance criteria, dependencies, and referenced sprint documents. The plan's architecture and the task's Note are the authority over generic examples in this skill.

## Non-negotiable guardrails

1. **Independent gate:** never approve a task that was not already in `[ ] QA Review`; never approve based solely on the implementer's claim.
2. **Zero compromise:** any confirmed security defect, architecture/layer violation, secret or unsafe hardcode, missing input validation, unhandled error/null path, material resource leak, or unmet acceptance criterion is a rejection. Do not waive technical debt silently.
3. **Read-only application audit:** inspect files, run checks, and update only the execution matrix and audit log. Never edit, format, refactor, or delete application source, tests, migrations, configuration, generated, vendor, or binary files. Do not stage or commit changes.
4. **Actionable evidence:** every rejection finding must include the repository-relative file, exact line or line range, violated rule, impact, and concrete required remediation. Use `nl -ba` or an equivalent line-numbered view; do not guess line numbers.
5. **No false certainty:** distinguish PASS, FAIL, and NOT RUN/BLOCKED for each check. A missing tool is not evidence of a clean result; use a documented repository alternative when available and disclose limitations.

## Required audit workflow

### 1. Load context and evidence

1. Read the complete matrix and the selected task.
2. Read the latest relevant entry in root `WALKTHROUGH_LOG.md`; if absent, inspect the plan-directory log. Use the log to identify claimed changed files and verification, but verify claims independently.
3. Read `.ai/planning/<plan-name>/README.md`, the task's referenced sprint section, and any directly referenced contract/ADR/test-plan documents.
4. Inspect `git status --short`, the task-related diff (prefer the log's files and `git diff`, then relevant history such as `git show`/`git diff HEAD~1` when appropriate), and the current contents of every in-scope file. Do not treat unrelated working-tree changes as task evidence.
5. Map each acceptance criterion and DoD item to code, tests, documentation, and an actual command result. Missing evidence is a finding when the criterion requires it.

### 2. Run proportionate verification

Detect the stack from manifests and use the narrowest relevant checks plus the project's configured suite. Run the security/static checks below when applicable; adapt only to an available, documented equivalent:

| Stack | Required audit checks when applicable |
|---|---|
| Go | `gosec ./...`; `golangci-lint run` |
| Node.js/TypeScript | `npm audit`; `npx eslint .` (use the repository package manager/scripts if configured) |
| Python | `bandit -r .`; `flake8` |
| Dart/Flutter | `flutter analyze --fatal-infos` |
| Firmware/C++/PlatformIO | configured native tests, `pio test -e native`, and relevant build target such as `pio run -e esp32-s3-devkitc-1`; inspect secret tracking |

Also run focused tests and compile/type checks required by the plan. Do not modify dependencies or lockfiles to make an audit pass. Record exact commands, exit status, and material output. Avoid scanning secrets into the response; identify only the file and safe description of the exposure.

### 3. Evaluate four pillars

Review the diff and surrounding code, not only changed lines, against the actual architecture baseline:

1. **Architecture and conventions:** correct layer and ownership; no DB/I/O/business logic in the wrong boundary; no direct GPIO or scheduler ownership where the plan forbids it; DRY; functions over 50 lines unless explicitly justified; naming, contracts, and tests follow the plan.
2. **Security hardening:** no tracked API keys, tokens, passwords, private keys, DB URIs, or embedded credentials; configuration comes from approved environment/provisioning mechanisms. Validate, type-check, bound, and sanitize external input; use parameterized queries and safe output encoding; reject malformed, unauthenticated, replayed, oversized, or unknown protocol data fail-closed. Do not expose internal errors or secrets in responses/logs.
3. **Robustness and edge cases:** null/nil and malformed input paths; timeouts, retries, duplicate/replay, reboot, stale state, partial failure, fault latch, safe defaults, cleanup/close/defer behavior, and error propagation. For this project, verify RF authentication/anti-replay, bounded parser, node boot safe-off, lease expiry, MEGA8 schedule ownership, temporary override resume, normalized-only persistence, and 4-node scope whenever the task touches them.
4. **Performance and resources:** N+1 queries or I/O in loops; unbounded queues/retries/allocation/logging; blocking work in ISR/callback; connection/stream/file cleanup; practical memory/CPU and latency constraints.

Classify findings as `BLOCKER`, `HIGH`, `MEDIUM`, or `LOW`. Under the zero-compromise rule, any material violation or unmet mandatory criterion rejects the task; explain why severity does not permit approval.

## Decision and state mutation

Only after completing the audit and recording evidence:

### Reject

Change only the selected task's status from `[ ] QA Review` to `[ ] In Progress`. Prepend an audit record to the applicable `WALKTHROUGH_LOG.md` (preserve prior entries exactly) containing the verdict, commands/results, and numbered actionable findings. Use this output shape:

```text
[AUDIT REJECTED] Task <TASK_ID>: <task name>
Lý do từ chối:
1. File: path/to/file.ext (Dòng XX–YY)
   - Mức độ: BLOCKER/HIGH/MEDIUM/LOW
   - Lỗi vi phạm: <specific rule and impact>
   - Bằng chứng: <observed code, test result, or missing criterion>
   - Hướng khắc phục bắt buộc: <concrete change and required verification>
Vui lòng chạy '/task-fix <TASK_ID>' kèm nội dung phản hồi trên.
```

### Approve

Approve only when all four pillars, every applicable acceptance criterion, required checks, and evidence pass with no open material finding. Change only the selected task's status to `[x] Done`. Prepend/update the matching newest log entry with:

```markdown
* **Trạng thái:** `[x] Done (Đã kiểm toán & Duyệt bởi Auditor)`
* **Audit Verdict:** LGTM - 100% DoD Compliant (Security: Clean | Arch: Clean)
```

Do not rewrite or delete the implementation log. If the matrix/log cannot be updated without risking unrelated content, stop before claiming approval and report the blocker.

## Exit checklist

Before finishing, verify with line-numbered reads and `git diff` that:

- the target was initially `[ ] QA Review`;
- all four pillars and all applicable task criteria were evaluated;
- actual commands and results are recorded, including unavailable/skipped checks;
- rejection has file, line, rule, impact, and remediation for every finding; or approval has no open material finding;
- exactly the intended matrix status and audit log entry changed, with valid UTF-8 preserved and no application code changed;
- the final status is `[ ] In Progress` on reject or `[x] Done` on approve.

Approval output:

```text
[AUDIT APPROVED - LGTM] Task <TASK_ID> đã được duyệt thành công!
Trạng thái cập nhật: [x] Done.
Sẵn sàng thực thi task tiếp theo bằng lệnh '/task-exec'.
```
