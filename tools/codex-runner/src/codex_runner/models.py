from dataclasses import dataclass
from enum import Enum
from typing import Literal
class TaskStatus(str, Enum):
    PENDING="Pending"; IN_PROGRESS="In Progress"; QA_REVIEW="QA Review"; DONE="Done"; REVALIDATE="Re-validate"
@dataclass(frozen=True)
class Task:
    task_id:str; title:str; status:TaskStatus; row_number:int; raw_row:str; acceptance_text:str
@dataclass(frozen=True)
class CommandResult:
    argv:tuple[str,...]; returncode:int|None; stdout:str; stderr:str; timed_out:bool=False; interrupted:bool=False
@dataclass(frozen=True)
class AuditResult:
    verdict:Literal["approved","rejected","unknown"]; feedback:str; source_output:str
