import asyncio,os,signal
from ..models import CommandResult
class CodexExecutor:
 def __init__(self,executable,args,timeout,graceful): self.base=(executable,*args); self.timeout=timeout; self.graceful=graceful
 async def run(self,prompt,log_dir):
  p=await asyncio.create_subprocess_exec(*self.base,prompt,stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE,start_new_session=True)
  out=[]; err=[]
  async def pump(stream,bag,label,file):
   with file.open("a",encoding="utf-8") as f:
    while True:
     b=await stream.readline()
     if not b: break
     s=b.decode("utf-8","replace"); bag.append(s); f.write(s); f.flush(); print(f"[{label}] {s}",end="")
  tasks=[asyncio.create_task(pump(p.stdout,out,"codex stdout",log_dir/"stdout.log")),asyncio.create_task(pump(p.stderr,err,"codex stderr",log_dir/"stderr.log"))]
  timed=False
  try: await asyncio.wait_for(asyncio.gather(*tasks),self.timeout)
  except asyncio.TimeoutError:
   timed=True; os.killpg(p.pid,signal.SIGTERM)
   try: await asyncio.wait_for(p.wait(),self.graceful)
   except asyncio.TimeoutError: os.killpg(p.pid,signal.SIGKILL); await p.wait()
  finally: await asyncio.gather(*tasks,return_exceptions=True)
  rc=await p.wait(); return CommandResult(self.base+(prompt,),rc,"".join(out),"".join(err),timed)
