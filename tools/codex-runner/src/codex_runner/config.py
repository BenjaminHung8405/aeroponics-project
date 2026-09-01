from dataclasses import dataclass
from pathlib import Path
import yaml
from .errors import ConfigError
@dataclass(frozen=True)
class Config:
    executable:str; exec_args:tuple[str,...]; timeout:float; graceful:float; max_retries:int; log_dir:Path; lock_file:Path; commands:dict; profiles:dict
def load(path:Path,root:Path,plan:str)->Config:
    try: d=yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    except (OSError,yaml.YAMLError) as e: raise ConfigError(f"invalid config: {e}") from e
    if not isinstance(d, dict): raise ConfigError("config root must be a mapping")
    c=d.get("codex",{}); r=d.get("runner",{}); mx=int(r.get("max_retries",3))
    if mx<0 or mx>3: raise ConfigError("max_retries must be between 0 and 3")
    if not isinstance(c, dict) or not isinstance(r, dict): raise ConfigError("codex and runner must be mappings")
    cmds=d.get("precheck_commands",{}); profiles=d.get("precheck_profiles",{})
    if not isinstance(cmds, dict) or not isinstance(profiles, dict): raise ConfigError("precheck configuration must be mappings")
    for name,x in cmds.items():
        if not isinstance(x, dict) or not isinstance(x.get("argv"),list) or not x["argv"] or not all(isinstance(v,str) and v for v in x["argv"]): raise ConfigError(f"invalid precheck argv: {name}")
        wd=(root/x.get("cwd",".")).resolve()
        if root.resolve() not in wd.parents and wd!=root.resolve(): raise ConfigError(f"precheck cwd escapes repository: {name}")
    selected=profiles.get(plan,{})
    if not isinstance(selected,dict): raise ConfigError(f"invalid precheck profile: {plan}")
    for name in selected.get("commands",[]):
        if name not in cmds: raise ConfigError(f"precheck profile references unknown command: {name}")
    return Config(str(c.get("executable","codex")),tuple(c.get("exec_args",["exec","--"])),float(c.get("timeout_seconds",3600)),float(c.get("graceful_shutdown_seconds",10)),mx,(root/r.get("log_directory",".codex-runner/runs")).resolve(),(root/r.get("lock_file",".codex-runner/runner.lock")).resolve(),cmds,profiles)
