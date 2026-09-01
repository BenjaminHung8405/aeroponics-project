import asyncio
from ..models import CommandResult
async def run_prechecks(cfg,root,plan,log_dir,dry=False):
 names=cfg.profiles.get(plan,{}).get("commands",[]); results=[]
 for name in names:
  c=cfg.commands[name]; argv=tuple(c["argv"]); cwd=(root/c.get("cwd",".")).resolve()
  if dry: print("DRY-RUN:"," ".join(argv)); continue
  try: p=await asyncio.create_subprocess_exec(*argv,cwd=cwd,stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
  except OSError as exc: results.append(CommandResult(argv,None,"",str(exc))); return False,results
  o,e=await p.communicate(); res=CommandResult(argv,p.returncode,o.decode("utf-8","replace"),e.decode("utf-8","replace")); results.append(res)
  print(f"[precheck {name}] exit={p.returncode}")
  if p.returncode and c.get("required",True): return False,results
 return True,results
