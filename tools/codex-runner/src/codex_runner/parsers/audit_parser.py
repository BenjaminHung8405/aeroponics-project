import re
from ..models import AuditResult
APP=re.compile(r"\[AUDIT APPROVED\s*-\s*LGTM\]|^\s*LGTM(?:\s|$)|^\s*Đã được duyệt",re.I|re.M)
REJ=re.compile(r"\[AUDIT REJECTED\]|^\s*REJECTED(?:\s|$)|^\s*Từ chối duyệt",re.I|re.M)
def parse_audit(text:str)->AuditResult:
    a=bool(APP.search(text)); r=bool(REJ.search(text))
    if a==r: return AuditResult("unknown","",text)
    if a: return AuditResult("approved","",text)
    marker=REJ.search(text)
    return AuditResult("rejected",text[marker.start():].strip() if marker else text.strip(),text)
