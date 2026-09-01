import re
from pathlib import Path
from ..errors import ParseError, StateError
from ..models import Task, TaskStatus
ID=re.compile(r"^[A-Za-z][A-Za-z0-9-]{0,31}$")
STATUS={"[ ] Pending":TaskStatus.PENDING,"[ ] In Progress":TaskStatus.IN_PROGRESS,"[ ] QA Review":TaskStatus.QA_REVIEW,"[x] Done":TaskStatus.DONE,"[!] Re-validate":TaskStatus.REVALIDATE}
def parse_progress(path:Path)->list[Task]:
    try: text=path.read_text(encoding="utf-8")
    except OSError as e: raise ParseError(f"cannot read tracker {path}: {e}") from e
    if not text.strip(): raise ParseError(f"tracker is empty: {path}")
    tasks=[]; header=None
    for n,line in enumerate(text.splitlines(),1):
        if line.startswith("|"):
            cells=[x.strip() for x in line.strip().strip("|").split("|")]
            if cells and any(x.lower()=="task id" for x in cells): header=cells; continue
            if cells and any(x.lower() in {"rule id", "status", "field", "value"} for x in cells) and not any(x.lower()=="task id" for x in cells):
                header = None
                continue
            if header and cells and all(cell and set(cell) <= set("-: ") for cell in cells): continue
            if header:
                if len(cells)!=len(header): raise ParseError(f"{path}:{n}: malformed table row")
                d=dict(zip(header,cells)); tid=d.get("Task ID","").strip("* ")
                if not tid: continue
                if not ID.fullmatch(tid): raise ParseError(f"{path}:{n}: invalid task id {tid!r}")
                raw=d.get("Status",""); status=STATUS.get(raw)
                if status is None: raise ParseError(f"{path}:{n}: invalid status {raw!r}")
                if any(t.task_id==tid for t in tasks): raise ParseError(f"{path}:{n}: duplicate task {tid}")
                tasks.append(Task(tid,d.get("Nội dung chính",d.get("Công việc",d.get("Mô tả Task", ""))),status,n,line,d.get("Done khi",d.get("Note / Chỉ thị kỹ thuật cấp cao", ""))))
    if not tasks: raise ParseError(f"no task table found: {path}")
    return tasks
def find_task(tasks, task_id):
    found=[t for t in tasks if t.task_id==task_id]
    if len(found)!=1: raise StateError(f"task {task_id!r} not found or ambiguous")
    return found[0]
def first_pending(tasks):
    return next((t for t in tasks if t.status is TaskStatus.PENDING),None)
def assert_expected_status(task, expected):
    if task.status is not expected: raise StateError(f"task {task.task_id} is {task.status.value}, expected {expected.value}")
