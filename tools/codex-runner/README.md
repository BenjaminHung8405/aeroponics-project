# codex-runner

Deterministic, sequential orchestrator for the repository Codex skills. It reads `.ai/planning/<plan>/PROGRESS.md`, never edits the tracker itself, executes each Codex operation in a fresh process, streams output, runs configured pre-checks before audit, and stops on ambiguous results or after three fixes.

```bash
python3.11 -m venv .venv-codex-runner
source .venv-codex-runner/bin/activate
pip install -e 'tools/codex-runner[dev]'
python -m codex_runner --plan aeroponics-lean --dry-run
python -m codex_runner --plan aeroponics-lean --auto-approve --max-retries 3
```

`--auto-approve` only suppresses interactive confirmation (the current implementation is non-interactive); it never marks a task Done. Only `/task-audit` may do that. The runner lock is `.codex-runner/runner.lock`; remove it only after verifying its PID is stale. Outputs are stored under `.codex-runner/runs/`. `--dry-run` performs no writes or subprocess execution.
