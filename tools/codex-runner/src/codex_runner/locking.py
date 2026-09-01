import json,os,socket
from pathlib import Path
from .errors import RunnerError
class Lock:
 def __init__(self,path,plan): self.path=path; self.plan=plan; self.held=False
 def __enter__(self):
  self.path.parent.mkdir(parents=True,exist_ok=True)
  try:
   fd=os.open(self.path,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
  except FileExistsError: raise RunnerError(f"runner lock exists: {self.path}; remove only if stale")
  os.write(fd,json.dumps({"pid":os.getpid(),"host":socket.gethostname(),"plan":self.plan}).encode()); os.close(fd); self.held=True; return self
 def __exit__(self,*_):
  if self.held:
   try:self.path.unlink()
   except FileNotFoundError:pass
