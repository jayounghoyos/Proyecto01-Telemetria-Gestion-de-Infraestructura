"""Run local functional gates and write actual unittest results as JSON."""
import datetime
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import time
import unittest
ROOT=Path(__file__).resolve().parents[1]
class Results(unittest.TextTestResult):
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs); self.rows=[]
    def startTest(self,test):
        self.started=time.monotonic(); super().startTest(test)
    def addSuccess(self,test):
        super().addSuccess(test); self.rows.append(dict(test=test.id(),result="PASS",seconds=round(time.monotonic()-self.started,3)))
    def addFailure(self,test,err):
        super().addFailure(test,err); self.rows.append(dict(test=test.id(),result="FAIL",detail=self._exc_info_to_string(err,test)))
    def addError(self,test,err):
        super().addError(test,err); self.rows.append(dict(test=test.id(),result="ERROR",detail=self._exc_info_to_string(err,test)))
if __name__=="__main__":
    suite=unittest.defaultTestLoader.discover(str(ROOT/"tests"),pattern="test_*.py")
    result=unittest.TextTestRunner(verbosity=2,resultclass=Results).run(suite)
    hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ("server","client","tests")
            for p in (ROOT/folder).rglob("*") if p.suffix in (".c",".h",".py")}
    report=dict(scope="LOCAL ONLY; no cloud, pcap or individual evidence",utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                platform=platform.platform(),python=sys.version,tests_run=result.testsRun,success=result.wasSuccessful(),results=result.rows,source_sha256=hashes)
    target=ROOT/"tests/results/functional.json"; target.parent.mkdir(exist_ok=True)
    target.write_text(json.dumps(report,indent=2)+"\n")
    sys.exit(0 if result.wasSuccessful() else 1)
