import argparse,asyncio
from pathlib import Path
from .config import load
from .locking import Lock
from .runner import Runner
from .errors import RunnerError
def main():
 p=argparse.ArgumentParser(prog="codex-runner"); p.add_argument("--plan"); p.add_argument("--task"); p.add_argument("--repo",type=Path,default=Path.cwd()); p.add_argument("--config",type=Path,default=Path("tools/codex-runner/config.yaml")); p.add_argument("--auto-approve",action="store_true"); p.add_argument("--max-retries",type=int); p.add_argument("--dry-run",action="store_true"); p.add_argument("--once",action="store_true"); a=p.parse_args(); root=a.repo.resolve(); config_path=a.config if a.config.is_absolute() else (root/a.config)
 try:
  raw=__import__("yaml").safe_load(config_path.read_text(encoding="utf-8")); plan=a.plan or raw.get("runner",{}).get("default_plan","aeroponics-lean"); cfg=load(config_path,root,plan)
  if a.max_retries is not None: cfg=cfg.__class__(**{**cfg.__dict__,"max_retries":a.max_retries})
  if a.dry_run: return asyncio.run(Runner(root,plan,cfg,a).run_all())
  with Lock(cfg.lock_file,plan): return asyncio.run(Runner(root,plan,cfg,a).run_all())
 except (RunnerError,OSError,ValueError) as e: print(f"ERROR: {e}"); return 1
