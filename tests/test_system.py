"""Local-only integration tests. Starts and stops only its own server processes."""
import concurrent.futures
import importlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
import uuid
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "client"))
import telep
import node
HOST = "127.0.0.1"
MEAS = "TEMP=24;HUM=60;POWER=120;VIB=0.5;STATUS=OK"


def until(predicate, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = predicate()
        if value:
            return value
        time.sleep(.02)
    raise AssertionError("condition not reached before deadline")


def http(path):
    with urllib.request.urlopen("http://127.0.0.1:8080" + path, timeout=3) as r:
        body = r.read().decode()
        return json.loads(body) if path != "/" else body


def request(cmd, *fields):
    return telep.send_one_request(cmd, *fields, hostname=HOST)


def stats(id):
    reply = request("STATS", id)
    assert reply[0] == ["OK", "1"], reply
    return {k: int(v) for k, v in (x.split("=") for x in reply[1][2].split(";"))}


def register(id="N"):
    session = uuid.uuid4().hex
    assert request("HELLO", id, session)[0] == ["OK", "REGISTERED"]
    return session


def udp(id, session, seq, measurement=MEAS):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.sendto(telep.encode_message("TELEMETRY", id, session, seq, measurement), (HOST, 5001))


class SystemTests(unittest.TestCase):
    def start_server(self):
        self.server = subprocess.Popen([str(ROOT / "server/telemetry_server")], stdout=self.log, stderr=self.log)
        def ready():
            if self.server.poll() is not None:
                raise AssertionError("server exited on startup")
            try:
                return http("/status")["tcp_ok"] == 1
            except (OSError, urllib.error.URLError):
                return False
        until(ready)

    def setUp(self):
        # Refuse to attach to or kill an existing service.
        for port in (5000, 8080):
            with socket.socket() as s:
                s.settimeout(.1)
                if s.connect_ex((HOST, port)) == 0:
                    self.fail("port already in use; refusing to touch existing service")
        self.log = tempfile.TemporaryFile()
        self.start_server()

    def tearDown(self):
        self.server.terminate()
        try:
            self.server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.server.kill()
            self.server.wait()
        self.log.close()

    def test_http_routes_and_slow_headers(self):
        slow = socket.create_connection((HOST, 8080), timeout=4)
        try:
            slow.sendall(b"GET / HTTP/1.1\r\nHost:")
            start = time.monotonic()
            self.assertIn("Nodes and last measurements", http("/"))
            self.assertNotIn("ONLINE", http("/"))
            self.assertEqual(http("/nodes"), [])
            self.assertEqual(http("/alerts"), [])
            self.assertEqual(http("/status")["udp_ok"], 1)
            self.assertLess(time.monotonic() - start, 1.5)
            self.assertIn(b"408", slow.recv(4096))
        finally:
            slow.close()
        for frame, status in [(b"POST / HTTP/1.1\r\n\r\n", b"405"),
                              (b"GET /missing HTTP/1.1\r\n\r\n", b"404"),
                              (b"garbage\r\n\r\n", b"400"),
                              (b"GET / HTTP/1.1\r\nX: " + b"a"*1500 + b"\r\n\r\n", b"431")]:
            with socket.create_connection((HOST,8080),timeout=4) as s:
                s.sendall(frame)
                self.assertIn(status, s.recv(4096))

    def test_http_absolute_deadline(self):
        with socket.create_connection((HOST,8080),timeout=4) as s:
            s.sendall(b"GET / HTTP/1.1\r\nX: ")
            start=time.monotonic()
            for _ in range(5):
                time.sleep(.3)
                s.sendall(b"a")
            self.assertIn(b"408", s.recv(4096))
            self.assertLess(time.monotonic()-start, 3)

    def test_tcp_validation_and_recovery(self):
        bad = [b"GET_STATUS|extra\n", b"HELLO||abc\n", b"GET_NODES|\n", b"\n",
               b"GET_STATUS\x00ignored\n", b"HELLO|N|short\n", b"STATS|N|x\n",
               b"X"*1100+b"\n", b"BOGUS\n"]
        with socket.create_connection((HOST,5000),timeout=4) as s:
            f=s.makefile("rb")
            for line in bad:
                s.sendall(line)
                self.assertTrue(f.readline().startswith(b"ERR|"), line)
                s.sendall(b"GET_STATUS\n")
                self.assertTrue(f.readline().startswith(b"OK|boot_id="))
            f.close()

    def test_udp_validation(self):
        session=register()
        prefix=f"TELEMETRY|N|{session}|".encode()
        bad=[prefix+b"abc|"+MEAS.encode()+b"\n", prefix+b"-1|"+MEAS.encode()+b"\n",
             prefix+b"65536|"+MEAS.encode()+b"\n", prefix+b"0||"+MEAS.encode()+b"\n",
             prefix+b"0|TEMP=NaN;HUM=60;POWER=100\n", prefix+b"0|TEMP=Inf;HUM=60;POWER=100\n",
             prefix+b"0|TEMP=20;HUM=101;POWER=100\n", prefix+b"0|TEMP=20;HUM=50;UNKNOWN=1\n",
             prefix+b"0|TEMP=20;HUM=50;STATUS=MAYBE\n", prefix+b"0|TEMP=20;TEMP=21;HUM=50\n",
             prefix+b"0|TEMP=20;HUM=50;POWER=100;\n",prefix+b"0|"+MEAS.encode(),
             prefix+b"0|"+MEAS.encode()+b"\x00x\n", prefix+b"0|"+MEAS.encode()+b"|extra\n",
             prefix+b"0|"+MEAS.encode()+b"\n"+b"x"*2000, prefix+b"0|TEMP=1e999;HUM=50;POWER=100\n"]
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
            for packet in bad: s.sendto(packet,(HOST,5001))
        until(lambda: http("/status")["udp_invalid"] == len(bad))
        self.assertEqual(stats("N")["unique"],0)
        udp("N",session,0)
        until(lambda: stats("N")["unique"]==1)

    def test_stats_duplicates_reordering_and_late_tail(self):
        session=register()
        for seq in [0,2,1,3,2]:
            udp("N",session,seq)
        until(lambda: stats("N")["duplicates"]==1)
        self.assertEqual(request("REPORT","N",session,5,5,0,0,1)[0],["OK","REPORTED"])
        current=stats("N")
        self.assertEqual((current["unique"],current["duplicates"],current["reordered"],current["loss_estimated"]),(4,1,1,1))
        udp("N",session,4)
        until(lambda: stats("N")["loss_estimated"]==0)
        self.assertEqual(http("/nodes")[0]["statistics"],stats("N"))
        self.assertEqual(request("HELLO","N",session)[0],["OK","REGISTERED"])
        self.assertEqual(stats("N")["unique"],5)
        self.assertEqual(request("HELLO","N",uuid.uuid4().hex)[0],["ERR","105","ID_IN_USE"])
        self.assertEqual(request("REPORT","N",session,5,4,0,0,1)[0][0],"ERR")
        self.assertEqual(request("REPORT","N",session,5,4,1,0,1)[0][0],"ERR")
        # Old measurements must not replace the most recent sequence's data.
        udp("N",session,0,"TEMP=99;HUM=60;POWER=120")
        self.assertIn("TEMP=24.00",request("GET_LAST","N")[0][3])

    def test_node_without_drop(self):
        self.check_node(0,20,20)

    def test_node_known_drop(self):
        self.check_node(25,20,15)

    def check_node(self,drop,count,sent):
        result=subprocess.run([sys.executable,str(ROOT/"client/node.py"),"--host",HOST,"--id","SIM",
                               "--interval","0.01","--count",str(count),"--drop",str(drop)],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr)
        final=json.loads(result.stdout.strip().splitlines()[-1])
        self.assertEqual((final["attempts"],final["sent"],final["omitted"],final["send_errors"]),(count,sent,count-sent,0))
        self.assertTrue(final["final_ack"])
        until(lambda:stats("SIM")["unique"]==sent)
        self.assertEqual(stats("SIM")["loss_estimated"],0)

    def test_two_operators_alerts_while_udp_continues(self):
        session=register()
        gate=threading.Barrier(3)
        def operator():
            with telep.TelepConnection(HOST) as c:
                self.assertTrue(c.subscribe())
                gate.wait(timeout=3)
                alerts=[]
                for _ in range(50):
                    self.assertEqual(c.request("GET_NODES")[0],["OK","1"])
                    self.assertEqual(c.request("GET_STATUS")[0][0],"OK")
                    alerts.extend(c.take_alerts())
                    time.sleep(.01)
                alerts.extend(c.take_alerts())
                self.assertGreater(len(alerts),0)
                self.assertTrue(all(len(a)==4 for a in alerts))
                return len(alerts)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as ex:
            futures=[ex.submit(operator),ex.submit(operator)]
            gate.wait(timeout=3)
            for seq in range(80):
                udp("N",session,seq,f"TEMP={45 if seq%2 else 24};HUM=60;POWER=120")
                time.sleep(.01)
            for f in futures: f.result(timeout=10)
        self.assertEqual(stats("N")["unique"],80)
        self.assertEqual(http("/status")["alerts"],40)

    def test_slow_subscriber_queue_does_not_block_udp(self):
        session=register()
        with socket.create_connection((HOST,5000),timeout=5) as slow:
            slow.sendall(b"SUBSCRIBE\n")
            self.assertIn(b"SUBSCRIBED",slow.recv(1024))
            # Hold its owner in partial command reading; the alert queue must disconnect it.
            slow.sendall(b"GET_")
            for seq in range(200):
                udp("N",session,seq,f"TEMP={45 if seq%2 else 24};HUM=60;POWER=120")
                time.sleep(.002)
            until(lambda:http("/status")["slow_disconnected"]>=1)
            until(lambda:stats("N")["unique"]==200)
            self.assertEqual(http("/status")["udp_ok"],1)
            self.assertEqual(request("GET_STATUS")[0][0],"OK")

    def test_node_recovers_after_server_restart(self):
        child=subprocess.Popen([sys.executable,str(ROOT/"client/node.py"),"--id","RECOVER","--host","localhost",
                                "--interval","0.05","--count","90","--dns-refresh","1"],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            until(lambda:http("/status")["unique"]>5)
            old_boot=http("/status")["boot_id"]
            self.server.terminate(); self.server.wait(timeout=3)
            self.start_server()
            self.assertNotEqual(http("/status")["boot_id"],old_boot)
            out,err=child.communicate(timeout=12)
            self.assertEqual(child.returncode,0,err)
            self.assertIn("server_restart_previous_session",out)
            self.assertGreater(stats("RECOVER")["unique"],0)
        finally:
            if child.poll() is None: child.terminate(); child.communicate(timeout=5)

    def test_invalid_client_arguments(self):
        for args in [["--interval","-1"],["--drop","nan"],["--drop","101"],["--spike","TEMP=nan"],["--spike","ABC=1"]]:
            result=subprocess.run([sys.executable,str(ROOT/"client/node.py"),"--id","BAD",*args],capture_output=True,timeout=3)
            self.assertEqual(result.returncode,2)

    def test_gui_reconnect_controller(self):
        # Controller test without a display. This does not claim visual GUI evidence.
        import types
        try:
            import tkinter
        except ImportError:
            tk=types.ModuleType("tkinter")
            tk.messagebox=Mock(); tk.ttk=Mock()
            sys.modules["tkinter"]=tk
        gui=importlib.import_module("operator_gui")
        w=gui.OperatorWindow.__new__(gui.OperatorWindow)
        w.root=Mock(); w.refresh_timer=None; w.status_label=Mock(); w.connection=Mock()
        w.connection.socket=object(); w._refresh=Mock(side_effect=ConnectionError("gone"))
        w.refresh()
        w.connection.close.assert_called_once()
        w.root.destroy.assert_not_called()
        w.root.after.assert_called_once()
        w.connection.socket=None
        with self.assertRaises(ConnectionError): w.request("GET_LAST", "N")
        w._refresh=Mock(); w.connection.subscribe.return_value=True
        w.refresh()
        w.connection.connect.assert_called_once()
        w._refresh.assert_called_once()

    def test_gui_mid_refresh_failures(self):
        import types
        try:
            import tkinter
        except ImportError:
            tk=types.ModuleType("tkinter"); tk.messagebox=Mock(); tk.ttk=Mock()
            sys.modules["tkinter"]=tk
        gui=importlib.import_module("operator_gui")
        for failing in ("GET_NODES", "GET_LAST", "GET_ALERTS"):
            w=gui.OperatorWindow.__new__(gui.OperatorWindow)
            w.root=Mock(); w.refresh_timer=None; w.status_label=Mock(); w.last_alert_label=Mock()
            w.node_table=Mock(); w.alert_table=Mock(); w.replace_rows=Mock()
            w.connection=Mock(); w.connection.socket=object(); w.connection.take_alerts.return_value=[]
            def response(command,*args):
                if command==failing: raise ConnectionError("lost during " + command)
                if command=="GET_STATUS": return [["OK","active=1"]]
                if command=="GET_NODES": return [["OK","1"],["N","ACTIVE","0"]]
                if command=="GET_LAST": return [["OK","N","1",MEAS]]
                return [["OK","0"]]
            w.connection.request.side_effect=response
            w.refresh()
            w.connection.close.assert_called_once()
            w.root.destroy.assert_not_called()
            w.root.after.assert_called_once()

    def test_dns_resolved_on_every_connection(self):
        from unittest.mock import patch
        original=socket.getaddrinfo
        with patch("telep.socket.getaddrinfo",wraps=original) as resolve:
            for _ in range(3):
                with telep.TelepConnection("localhost") as c:
                    self.assertEqual(c.request("GET_STATUS")[0][0],"OK")
            self.assertEqual(resolve.call_count,3)
