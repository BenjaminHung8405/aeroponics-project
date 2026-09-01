---
name: plan-matrix
description: Build or refresh the repository-root PROGRESS.md execution matrix from .ai/planning/<plan-name>/README.md and sprint documents. Use for explicit /plan-matrix requests; do not implement application code.
metadata:
  short-description: Convert sprint plans into a constraint-driven execution matrix
---

# `/plan-matrix`: Task & Constraint Architect

Act as a lead architect and quality-governance agent. Convert the selected planning documents into one executable, reviewable `PROGRESS.md` at the repository root.

## Invocation

```text
/plan-matrix <plan_name> [target_sprint]
```

- `plan_name` is required and refers to `.ai/planning/<plan_name>/` exactly. Do not silently normalize it or search unrelated plans.
- `target_sprint` is optional. If supplied, load only that file (for example `sprint_1.md`); otherwise load every `sprint_*.md` in natural numeric order, including variants such as `sprint_1_5.md`.

## Hard boundaries

1. **Zero application code:** read planning files and repository metadata only as needed. The only file this skill may create or modify is `<repository-root>/PROGRESS.md`.
2. **Single source of truth:** never write or update `.ai/planning/<plan_name>/PROGRESS.md`; the output is always at the repository root.
3. **Pending baseline:** every task newly represented in the generated matrix must have exactly `[ ] Pending`. Do not infer completion from source files, old progress files, or sprint prose.
4. **No generic notes:** every task's Note must contain at least two concrete, verifiable constraints. Reject notes such as “write clean code”, “add security”, or “handle errors” unless made measurable and implementation-specific.

## Workflow

### 1. Validate and ingest

1. Resolve the repository root from the current workspace before writing.
2. Confirm `.ai/planning/<plan_name>/` and its `README.md` exist. Stop with a clear error if either is missing.
3. Read the complete `README.md` for confirmed tech stack, architecture, directory conventions, security, error handling, and performance rules.
4. Read the selected sprint file(s) completely. If a requested sprint does not exist, stop without changing `PROGRESS.md`.
5. Extract every concrete task, its source sprint, dependencies, named paths/symbols, and hard review criteria. Do not invent unsupported product requirements; where a constraint is absent, derive a testable constraint from the documented architecture and mark assumptions explicitly in the Note.

### 2. Synthesize the matrix

- Group tasks by technical layer using the sprint's tracks. Preserve the sprint's track meaning; use sensible names such as Data/Entity, Domain/Usecase, API/UI/Delivery, and Operations/Integration.
- Assign globally unique IDs sequentially within each track (`A1`, `A2`, `B1`, `B2`, ...). If multiple sprints contribute to a track, continue the same sequence rather than restarting it.
- Keep each task atomic and retain its concrete target path and named symbol in the description when present.
- For every task, write a Note containing at least two specific constraints selected from the following, grounded in README/sprint criteria:
  - required pattern, boundary, interface, schema/index, or encapsulation rule;
  - measurable validation, sanitization, authorization, secret/configuration, or zero-hardcoding rule;
  - explicit edge-case, null-safety, transaction/rollback, concurrency, timeout, idempotency, or resource limit;
  - observable verification such as an exact test, status code, invariant, metric, or query restriction.
- Cite the source in the track's `Nguồn phân rã` line and include sprint provenance in the task description or Note when tasks are merged.

### 3. Write `PROGRESS.md`

Create or replace only the root file using UTF-8. Use this structure:

```markdown
# TIẾN ĐỘ THỰC THI HỆ THỐNG
## Started
* **Thời gian kích hoạt:** YYYY-MM-DD HH:mm (ICT)
* **Execution Agent phụ trách:** Codex Execution Agent

## Reference Plan
* **Đường dẫn gốc:** `.ai/planning/<plan_name>/`
* **Tệp tin Sprint đang chạy:** `<comma-separated selected files>`

## Addition Plan
* *[Ghi chú]*: Chưa có yêu cầu phát sinh từ phía lập trình viên con người.

---

## Track A: [technical-layer name]
*Nguồn phân rã:* `.ai/planning/<plan_name>/sprint_*.md`

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| A1 | ... | `[ ] Pending` | ... |
```

Include one section per non-empty track and no fabricated empty tracks. Use the current ICT timestamp in the specified format. Keep Vietnamese Unicode intact.

## Verification before reporting

Inspect the resulting file and verify all of the following:

- It exists at the repository root and only that file was changed by this skill.
- Every task ID matches `^[A-Z]+[0-9]+$` and is globally unique.
- Every task has a non-empty Note with at least two verifiable, non-generic constraints.
- 100% of task statuses are exactly `[ ] Pending`.
- All loaded sprint tasks are represented, with no accidental task duplication.
- Track and task totals are counted from the final table, not guessed.

Report exactly:

```text
Đã khởi tạo ma trận PROGRESS.md thành công cho plan '<plan_name>'.
Tổng số Track: X | Tổng số Task: Y.
Sẵn sàng thực thi. Vui lòng chạy '/task-exec' để bắt đầu xử lý Task Pending đầu tiên.
```

If validation fails, fix the matrix before reporting; do not claim success.
