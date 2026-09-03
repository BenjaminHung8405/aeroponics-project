---
name: auto-dev
description: Orchestrate plan creation, task execution, independent audit, and bounded remediation through isolated Codex CLI subprocesses.
metadata:
  short-description: Run the complete autonomous plan-to-QA loop
---

# `/auto-dev`: Autonomous Plan-to-QA Orchestrator

`/auto-dev` is a controller, not an implementation agent. It coordinates the existing
`/plan-scaffold`, `/plan-matrix`, `/task-exec`, `/task-audit`, and `/task-fix` skills by
starting one fresh `codex exec` subprocess per delegated operation. The controller never
edits task checkboxes, application code, tests, migrations, or configuration itself.

## Invocation

```text
/auto-dev <plan_name> "<plan_description>" [--max-retries 3] [--dry-run]
```

Arguments:

- `plan_name` is required. It is passed unchanged to `/plan-scaffold`; the scaffold skill
  owns normalization and conflict detection.
- `plan_description` is required for the planning pipeline. It is used only when the plan
  directory does not yet exist. In `--dry-run`, it is still validated but no subprocess is
  launched and no file is changed.
- `--max-retries N` is optional and defaults to `3`. `N` must be an integer in `1..3`; the
  hard upper bound cannot be overridden.
- `--dry-run` validates arguments, repository state, and the proposed command sequence,
  then exits without mutation.

Environment configuration:

```bash
# Optional model selected by the local Codex installation.
export AUTO_DEV_MODEL="gpt-5.5"

# Optional space-separated commands executed before every /task-audit.
# Use shell-quoted commands when a command contains spaces or operators.
export AUTO_DEV_PRECHECKS='git diff --check'

# Optional timeout for each delegated subprocess.
export AUTO_DEV_TIMEOUT_SECONDS=1800
```

`AUTO_DEV_MODEL` must name a model available to the local CLI. A missing model or CLI is a
hard stop; the controller must not silently substitute a different model. `AUTO_DEV_PRECHECKS`
should contain the repository's deterministic lint, compile, and test commands. Every
configured command must exit `0` before an audit handoff.

## Preconditions

1. Run from inside the target repository; resolve the root with `git rev-parse --show-toplevel`.
2. `codex` is installed and `codex exec --help` succeeds.
3. `.codex/skills/plan-scaffold/SKILL.md`, `plan-matrix/SKILL.md`, `task-exec/SKILL.md`,
   `task-audit/SKILL.md`, and `task-fix/SKILL.md` exist.
4. The working tree may contain unrelated changes, but the controller must preserve them.
5. For an existing plan, `.ai/planning/<plan_name>/README.md` and its sprint documents must
   be readable. A conflicting or incomplete plan stops the run rather than being overwritten.

## Postconditions

- Planning mode leaves a validated `.ai/planning/<plan_name>/` and root `PROGRESS.md`.
- Each successful execution/audit cycle leaves the delegated task in `[x] Done`.
- A rejected task is retried only through `/task-fix`, then re-audited from a fresh process.
- No task is marked Done by this controller, and no checkbox is edited by its shell script.
- Completion is reported only after re-reading `PROGRESS.md` and confirming no actionable
  `[ ] Pending`, `[ ] In Progress`, or `[ ] QA Review` task remains in scope.
- A circuit-breaker or any ambiguous/failed handoff leaves the repository untouched by the
  controller after the last delegated operation and prints a human-intervention warning.

## Isolation and communication architecture

### Process boundary

The controller invokes each skill using a separate process:

```bash
(cd "$REPO_ROOT" && codex exec --model "$AUTO_DEV_MODEL" \
  --dangerously-bypass-approvals-and-sandbox -- "$PROMPT")
```

The child receives only the prompt, current repository files, and explicitly supplied audit
feedback. It does not receive the parent's conversation history. This is the context-isolation
mechanism: after every task, the child process exits, its output is stored in a bounded log,
and the next child starts with a clean context window.

The controller passes durable state through repository artifacts only:

- planning documents under `.ai/planning/<plan_name>/`;
- root `PROGRESS.md` as the state machine;
- root `WALKTHROUGH_LOG.md` as evidence/audit history;
- a temporary run directory containing command output and retry counters.

Do not pass the complete prior conversation to a child. For `/task-fix`, pass only the
complete actionable audit verdict, bounded by `AUTO_DEV_MAX_FEEDBACK_BYTES` (default 128 KiB).
If the verdict exceeds the bound, stop instead of truncating remediation requirements.

### Session cleanup

Each run creates `${TMPDIR:-/tmp}/codex-auto-dev/<run-id>` with mode `0700`. It stores only
stdout/stderr, exit codes, and retry metadata. A trap removes the directory on normal exit;
`--keep-run-dir` is intentionally not supported so stale context cannot accumulate. On a
crash, a later run removes directories older than 24 hours before starting. Repository files
remain the source of truth.

### Delegation contract

The child skill must perform its own state transition and evidence logging. The controller
only verifies the result after the child exits:

| Delegation | Required exit state | Controller validation |
|---|---|---|
| `/plan-scaffold` | Plan directory exists | README plus at least one non-empty `sprint_*.md`; no PROGRESS created by scaffold |
| `/plan-matrix` | Root matrix exists | every represented task is `[ ] Pending` and IDs are unique |
| `/task-exec <id>` | target is `[ ] QA Review` | exact target row re-read; no other row may unexpectedly change |
| `/task-audit <id>` | `[x] Done` or `[ ] In Progress` | output verdict agrees with matrix state |
| `/task-fix <id> <feedback>` | target is `[ ] QA Review` | feedback was non-empty and fix did not self-approve |

Unknown output, non-zero exit, timeout, contradictory status, or an absent required log entry
is a hard stop. Never infer success from prose alone.

## State machine

```text
START
  ├─ existing valid plan? ─ yes ─> validate matrix
  └─ no ─> /plan-scaffold ─> validate artifacts
                         └─> /plan-matrix ─> validate PROGRESS

LOOP
  ├─ Pending task? ─ no ─> QA Review/In Progress task? ─ yes ─> STOP (manual state)
  │                                                └─ no ─> DONE
  └─ /task-exec TASK_ID ─> state == QA Review and prechecks exit 0?
                             └─ no ─> STOP
                             └─ yes ─> /task-audit TASK_ID
                                           ├─ LGTM + [x] Done ─> next Pending
                                           └─ REJECT + [ ] In Progress
                                                 ├─ retry count < limit ─> /task-fix
                                                 │       └─ [ ] QA Review ─> /task-audit
                                                 └─ retry count == limit ─> CIRCUIT BREAKER
```

The retry counter is per task ID and counts rejected audit verdicts. It is reset only when
that task reaches `[x] Done`. A fixer's claim is never treated as an audit approval.

## Deterministic parsing rules

The controller recognizes task rows in Markdown tables with this shape:

```text
| R5-M | description | `[ ] Pending` | note |
```

The accepted task ID is `[A-Za-z][A-Za-z0-9]*(?:-[A-Za-z0-9]+)*`; status must be one of the
literal tokens `[ ] Pending`, `[ ] In Progress`, `[ ] QA Review`, or `[x] Done`. Selection is
document order, not lexical order. The controller never changes these tokens with `sed` or
`perl`; only delegated skills may mutate them.

Use this exact read-only selector to select the first Pending row:

```bash
awk -F'|' '
  function trim(s) { gsub(/^[[:space:]]+|[[:space:]]+$/, "", s); return s }
  {
    id=trim($2); status=trim($4)
    gsub(/^`|`$/, "", status)
    if (id ~ /^[A-Za-z][A-Za-z0-9]*(-[A-Za-z0-9]+)*$/ && status == "[ ] Pending") {
      print id; exit
    }
  }
' PROGRESS.md
```

For a selected ID, exact state verification must match the ID field and status field rather
than grep the task description:

```bash
awk -F'|' -v wanted="$TASK_ID" '
  function trim(s) { gsub(/^[[:space:]]+|[[:space:]]+$/, "", s); return s }
  {
    id=trim($2); status=trim($4); gsub(/^`|`$/, "", status)
    if (id == wanted) { print status; found=1; exit }
  }
  END { if (!found) exit 2 }
' PROGRESS.md
```

## Reference implementation

The following Bash controller is the implementation blueprint for the skill. It is designed
to be pasted into the `/auto-dev` execution environment. It uses no direct checkbox edits.

```bash
#!/usr/bin/env bash
set -Eeuo pipefail
IFS=$'\n\t'

usage() {
  printf 'Usage: /auto-dev <plan_name> "<plan_description>" [--max-retries N] [--dry-run]\n' >&2
  exit 2
}

PLAN_NAME="${1:-}"; PLAN_DESCRIPTION="${2:-}"; shift 2 || usage
MAX_RETRIES=3; DRY_RUN=0
while (($#)); do
  case "$1" in
    --max-retries) [[ "${2:-}" =~ ^[1-3]$ ]] || usage; MAX_RETRIES="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    *) usage ;;
  esac
done
[[ -n "$PLAN_NAME" && -n "$PLAN_DESCRIPTION" ]] || usage

ROOT="$(git rev-parse --show-toplevel)" || { echo 'Not a Git repository.' >&2; exit 1; }
cd "$ROOT"
PLAN_DIR=".ai/planning/$PLAN_NAME"
MATRIX="$ROOT/PROGRESS.md"
MODEL="${AUTO_DEV_MODEL:-gpt-5.5}"
TIMEOUT="${AUTO_DEV_TIMEOUT_SECONDS:-1800}"
MAX_FEEDBACK="${AUTO_DEV_MAX_FEEDBACK_BYTES:-131072}"
RUN_ID="$(date +%Y%m%d%H%M%S)-$$"
RUN_DIR="${TMPDIR:-/tmp}/codex-auto-dev/$RUN_ID"
mkdir -p "$RUN_DIR"; chmod 700 "$RUN_DIR"
declare -A RETRIES=()
trap 'rm -rf -- "$RUN_DIR"' EXIT

die() { printf '[AUTO-DEV STOPPED] %s\n' "$*" >&2; exit 1; }
log() { printf '[auto-dev] %s\n' "$*"; }

command -v codex >/dev/null || die 'codex CLI is unavailable.'
codex exec --help >/dev/null 2>&1 || die 'codex exec is unavailable.'
for skill in plan-scaffold plan-matrix task-exec task-audit task-fix; do
  [[ -f ".codex/skills/$skill/SKILL.md" ]] || die "Missing .codex/skills/$skill/SKILL.md"
done

delegate() {
  local label="$1" prompt="$2" out="$RUN_DIR/$label.log" rc
  log "Launching $label in an isolated Codex process"
  set +e
  timeout --signal=TERM --kill-after=10s "$TIMEOUT" \
    codex exec --model "$MODEL" --dangerously-bypass-approvals-and-sandbox -- "$prompt" \
    >"$out" 2>&1
  rc=$?
  set -e
  printf '%s\n' "$rc" >"$RUN_DIR/$label.exit"
  cat "$out"
  [[ "$rc" -eq 0 ]] || die "$label failed with exit code $rc; see $out"
}

status_of() {
  local task_id="$1"
  awk -F'|' -v wanted="$task_id" '
    function trim(s) { gsub(/^[[:space:]]+|[[:space:]]+$/, "", s); return s }
    { id=trim($2); s=trim($4); gsub(/^`|`$/, "", s)
      if (id == wanted) { print s; found=1; exit } }
    END { if (!found) exit 2 }
  ' "$MATRIX"
}

first_pending() {
  awk -F'|' '
    function trim(s) { gsub(/^[[:space:]]+|[[:space:]]+$/, "", s); return s }
    { id=trim($2); s=trim($4); gsub(/^`|`$/, "", s)
      if (id ~ /^[A-Za-z][A-Za-z0-9]*(-[A-Za-z0-9]+)*$/ && s == "[ ] Pending") { print id; exit } }
  ' "$MATRIX"
}

count_status() { awk -F'|' -v wanted="$1" '
  function trim(s) { gsub(/^[[:space:]]+|[[:space:]]+$/, "", s); return s }
  { s=trim($4); gsub(/^`|`$/, "", s); if (s == wanted) n++ } END { print n+0 }
' "$MATRIX"; }

validate_plan() {
  [[ -f "$PLAN_DIR/README.md" ]] || die "Missing $PLAN_DIR/README.md"
  compgen -G "$PLAN_DIR/sprint_*.md" >/dev/null || die "No sprint document in $PLAN_DIR"
  [[ -s "$MATRIX" ]] || die 'Root PROGRESS.md is missing or empty.'
  local total pending id
  total="$(awk -F'|' 'NF >= 4 { n++ } END { print n+0 }' "$MATRIX")"
  pending="$(count_status '[ ] Pending')"
  (( total > 0 )) || die 'PROGRESS.md contains no task rows.'
  log "Validated matrix: $total task rows, $pending Pending"
}

run_prechecks() {
  local checks="${AUTO_DEV_PRECHECKS:-git diff --check}" cmd i=0 rc
  while IFS= read -r cmd; do
    [[ -n "$cmd" ]] || continue
    i=$((i+1)); log "Precheck $i: $cmd"
    set +e; bash -lc "$cmd"; rc=$?; set -e
    (( rc == 0 )) || die "Deterministic precheck failed (exit $rc): $cmd"
  done <<< "$checks"
}

if [[ ! -d "$PLAN_DIR" ]]; then
  if (( DRY_RUN )); then log "DRY RUN: would call /plan-scaffold $PLAN_NAME"; exit 0; fi
  delegate 01-plan-scaffold "/plan-scaffold $PLAN_NAME $(printf '%q' "$PLAN_DESCRIPTION")"
  [[ -d "$PLAN_DIR" ]] || die 'plan-scaffold did not create the requested plan directory.'
  [[ ! -e "$PLAN_DIR/PROGRESS.md" ]] || die 'plan-scaffold illegally created plan-local PROGRESS.md.'
  delegate 02-plan-matrix "/plan-matrix $PLAN_NAME"
else
  log "Using existing plan $PLAN_DIR; scaffold is skipped."
  [[ -f "$PLAN_DIR/README.md" ]] || die 'Existing plan is incomplete: README.md missing.'
  [[ -f "$MATRIX" ]] || { (( DRY_RUN )) || delegate 02-plan-matrix "/plan-matrix $PLAN_NAME"; }
fi

(( DRY_RUN )) && { validate_plan; log 'DRY RUN: validation complete; no mutation performed.'; exit 0; }
validate_plan

while :; do
  TASK_ID="$(first_pending || true)"
  if [[ -z "$TASK_ID" ]]; then
    in_progress="$(count_status '[ ] In Progress')"
    qa_review="$(count_status '[ ] QA Review')"
    if (( in_progress > 0 || qa_review > 0 )); then
      die "No Pending task, but $in_progress In Progress and $qa_review QA Review task(s) remain."
    fi
    log "COMPLETE: all tasks are [x] Done."
    exit 0
  fi

  log "Selected first Pending task: $TASK_ID"
  delegate "exec-$TASK_ID" "/task-exec $TASK_ID"
  [[ "$(status_of "$TASK_ID")" == '[ ] QA Review' ]] || die "/task-exec did not hand off $TASK_ID as QA Review."

  run_prechecks
  delegate "audit-$TASK_ID-${RETRIES[$TASK_ID]:-0}" "/task-audit $TASK_ID"
  state="$(status_of "$TASK_ID")"
  case "$state" in
    '[x] Done') RETRIES["$TASK_ID"]=0; log "AUDIT APPROVED: $TASK_ID"; ;;
    '[ ] In Progress')
      n=$(( ${RETRIES[$TASK_ID]:-0} + 1 )); RETRIES["$TASK_ID"]="$n"
      if (( n >= MAX_RETRIES )); then
        printf '[CIRCUIT BREAKER TRIGGERED] Task %s thất bại qua %d lần audit. Cần con người can thiệp.\n' "$TASK_ID" "$n" >&2
        exit 3
      fi
      feedback_file="$RUN_DIR/audit-$TASK_ID-${n}.log"
      cp "$RUN_DIR/audit-$TASK_ID-${n-1}.log" "$feedback_file" 2>/dev/null || true
      [[ -s "$feedback_file" ]] || die "Audit feedback for $TASK_ID is unavailable."
      (( $(wc -c <"$feedback_file") <= MAX_FEEDBACK )) || die "Audit feedback exceeds $MAX_FEEDBACK bytes."
      feedback="$(cat "$feedback_file")"
      delegate "fix-$TASK_ID-$n" "/task-fix $TASK_ID $(printf '%q' "$feedback")"
      [[ "$(status_of "$TASK_ID")" == '[ ] QA Review' ]] || die "/task-fix did not return $TASK_ID to QA Review."
      ;;
    *) die "Contradictory audit state for $TASK_ID: $state" ;;
  esac
done
```

## Operational rules

1. Never use `grep -q` alone to decide a handoff; verify the exact table row and status.
2. Never parse a successful audit from a child exit code alone. Require both the expected
   output marker (`[AUDIT APPROVED - LGTM]` or `[AUDIT REJECTED]`) and the matrix state.
3. Run deterministic checks after execution and before every audit. A non-zero linter,
   compiler, test runner, or configured rehearsal is a stop condition, not audit feedback.
4. Do not automatically fix an execution failure without an audit finding. `/task-fix` is
   reserved for a rejected audit with actionable feedback.
5. Do not run tasks in parallel: `PROGRESS.md` and `WALKTHROUGH_LOG.md` are shared mutable
   state, and parallel agents could select or overwrite the same task.
6. Treat `[ ] In Progress` without a fresh rejected audit as a manual-intervention state.
7. On circuit-breaker, print the task ID, retry count, last audit log path, and the exact next
   human action; do not reset the counter or force a checkbox.

## Completion report

After the final matrix re-read, print:

```text
=====================================================
AUTO-DEV HOÀN TẤT
- Kế hoạch: <plan_name>
- PROGRESS.md: mọi task đã ở trạng thái [x] Done
- Audit/fix retry: không vượt quá giới hạn cấu hình
=====================================================
```

An implementation of this skill should be added as `.codex/skills/auto-dev.md` exactly as
specified by this document. If the local Codex skill loader requires directory-based skills,
mirror the same content to `.codex/skills/auto-dev/SKILL.md`; do not silently change the
requested file path or alter the existing five sub-skills.
