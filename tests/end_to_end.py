"""Five real node processes and two persistent operator connections for >=120 seconds.
External runs are explicit (--host). --start-server creates only a local owned process.
"""
import argparse
import concurrent.futures
import datetime
import hashlib
import json
from pathlib import Path
import signal
import socket
import subprocess
import sys
import threading
import time
import uuid
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"client"))
import telep

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host",default="localhost")
    p.add_argument("--duration",type=int,default=120)
    p.add_argument("--start-server",action="store_true")
    args=p.parse_args()
    if args.duration<120: p.error("duration must be at least 120 seconds")
    if args.start_server and args.host not in ("localhost","127.0.0.1"): p.error("local server requires loopback host")
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out=ROOT/"tests/results"/("local-"+stamp if args.start_server else "external-"+stamp)
    out.mkdir(parents=True,exist_ok=False)
    server=None; nodes=[]; logs=[]; samples=[[],[]]; errors=[]; stop=threading.Event()
    ids=["E"+uuid.uuid4().hex[:8]+str(i) for i in range(5)]
    pool=concurrent.futures.ThreadPoolExecutor(max_workers=2)
    ready=[threading.Event(),threading.Event()]
    def operator(index):
        try:
            with telep.TelepConnection(args.host) as c:
                assert c.subscribe()
                ready[index].set()
                while not stop.is_set():
                    rows=c.request("GET_NODES")
                    assert rows[0][0]=="OK",rows
                    active={r[0] for r in rows[1:] if r[1]=="ACTIVE"}
                    status=c.request("GET_STATUS")
                    assert status[0][0]=="OK",status
                    samples[index].append(dict(monotonic=time.monotonic(),active_test_nodes=len(active.intersection(ids)),
                                               status=status[0][1],alerts=len(c.take_alerts())))
                    stop.wait(.5)
        except Exception as e:
            errors.append(repr(e)); ready[index].set(); stop.set()
    try:
        if args.start_server:
            for port in (5000,8080):
                with socket.socket() as sock:
                    if sock.connect_ex(("127.0.0.1",port))==0: raise RuntimeError("existing service; refusing to start")
            log=(out/"server.log").open("w");logs.append(log)
            server=subprocess.Popen([str(ROOT/"server/telemetry_server")],stdout=log,stderr=log)
            for _ in range(100):
                if server.poll() is not None: raise RuntimeError("server startup failed")
                try:
                    telep.send_one_request("GET_STATUS",hostname=args.host);break
                except OSError:time.sleep(.05)
            else:raise RuntimeError("server not ready")
        futures=[pool.submit(operator,i) for i in range(2)]
        for event in ready:
            if not event.wait(10) or errors:raise RuntimeError("operators not ready: "+repr(errors))
        for id in ids:
            log=(out/(id+".log")).open("w");logs.append(log)
            nodes.append(subprocess.Popen([sys.executable,str(ROOT/"client/node.py"),"--id",id,"--host",args.host,
                                          "--interval","0.2","--spike","TEMP=45"],stdout=log,stderr=log))
        deadline=time.monotonic()+15
        while not all(rows and rows[-1]["active_test_nodes"]==5 for rows in samples):
            if errors or time.monotonic()>deadline:raise RuntimeError("five nodes did not become active")
            time.sleep(.1)
        start=time.monotonic(); deadline=start+args.duration
        print("Five nodes and two operators ready; measuring",args.duration,"seconds",flush=True)
        while time.monotonic()<deadline:
            if errors or any(n.poll() is not None for n in nodes):raise RuntimeError("component stopped")
            time.sleep(.5)
        finish=time.monotonic()
        for n in nodes:n.send_signal(signal.SIGTERM)
        for n in nodes:
            if n.wait(timeout=15)!=0:raise RuntimeError("node final report failed")
        stop.set()
        for f in futures:f.result(timeout=10)
        rows=telep.send_one_request("STATS",hostname=args.host)
        statistics={r[0]:dict(x.split("=",1) for x in r[2].split(";")) for r in rows[1:] if r[0] in ids}
        for records in samples:
            measured=[r for r in records if start<=r["monotonic"]<=finish]
            assert len(measured)>=args.duration and all(r["active_test_nodes"]==5 for r in measured)
            first=dict(x.split("=") for x in measured[0]["status"].split(";"))
            last=dict(x.split("=") for x in measured[-1]["status"].split(";"))
            assert int(last["unique"])>int(first["unique"])
        assert len(statistics)==5
        for value in statistics.values():
            assert value["final_report"]=="1" and value["omitted"]=="0" and value["send_errors"]=="0"
            assert value["unique"]==value["sent"] and value["loss_estimated"]=="0",value
        assert all(sum(r["alerts"] for r in records)>=5 for records in samples)
        report=dict(success=True,scope="LOCAL WSL/process evidence" if args.start_server else "explicit external run",
                    host=args.host,utc=stamp,duration_seconds=finish-start,ids=ids,operators=samples,statistics=statistics,
                    source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                        for folder in ("server","client","tests") for p in (ROOT/folder).rglob("*") if p.suffix in (".c",".h",".py")})
        (out/"result.json").write_text(json.dumps(report,indent=2)+"\n")
        print("PASS:",out/"result.json",flush=True)
    except Exception as e:
        (out/"failure.json").write_text(json.dumps(dict(success=False,error=repr(e),operators=samples),indent=2)+"\n")
        raise
    finally:
        stop.set();pool.shutdown(wait=True)
        for n in nodes:
            if n.poll() is None:n.terminate()
        for n in nodes:
            try:n.wait(timeout=10)
            except subprocess.TimeoutExpired:n.kill();n.wait()
        if server is not None and server.poll() is None:server.terminate();server.wait(timeout=5)
        for log in logs:log.close()
if __name__=="__main__":main()
