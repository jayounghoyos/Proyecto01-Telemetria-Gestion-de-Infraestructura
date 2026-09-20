"""TELEP/2 node. DROP omits an attempt before sendto, deterministically."""
import argparse
import json
import math
import random
import re
import signal
import socket
import sys
import time
import uuid
import telep

LIMIT = 65536
SIMULATED_VARIABLES = {"TEMP": (24, 1.5), "HUM": (60, 4), "POWER": (120, 15), "VIB": (0.5, 0.2)}
RANGES = {"TEMP": (-100, 200), "HUM": (0, 100), "POWER": (0, 1000000), "VIB": (0, 1000)}

def parse_arguments():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--id", required=True)
    p.add_argument("--host", default=telep.DEFAULT_HOST)
    p.add_argument("--interval", type=float, default=2)
    p.add_argument("--count", type=int, default=LIMIT)
    p.add_argument("--drop", type=float, default=0, help="percentage omitted before send; deterministic schedule")
    p.add_argument("--dns-refresh", type=float, default=5, help="seconds between DNS/control-plane checks")
    p.add_argument("--session", default=uuid.uuid4().hex)
    p.add_argument("--spike", action="append", default=[])
    p.add_argument("--fail-status", action="store_true")
    a = p.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,15}", a.id): p.error("invalid ID")
    if not re.fullmatch(r"[0-9a-f]{32}", a.session): p.error("session needs 32 lowercase hex digits")
    if not math.isfinite(a.interval) or not 0.01 <= a.interval <= 3600: p.error("interval must be 0.01..3600")
    if not math.isfinite(a.dns_refresh) or not 1 <= a.dns_refresh <= 3600: p.error("dns-refresh must be 1..3600")
    if not math.isfinite(a.drop) or not 0 <= a.drop <= 100: p.error("drop must be 0..100")
    if not 1 <= a.count <= LIMIT: p.error("count must be 1..65536")
    a.forced = {}
    for spec in a.spike:
        try:
            key, value = spec.split("=", 1)
            key, value = key.upper(), float(value)
            low, high = RANGES[key]
            if not math.isfinite(value) or not low <= value <= high: raise ValueError()
            a.forced[key] = value
        except (ValueError, KeyError): p.error("invalid spike: " + spec)
    return a

def take_measurements(forced_values, fail_status):
    values = {k: min(RANGES[k][1], max(RANGES[k][0], random.gauss(*params))) for k, params in SIMULATED_VARIABLES.items()}
    values.update(forced_values)
    values["STATUS"] = "FAIL" if fail_status else "OK"
    return values

def omitted_by_drop(sequence, percentage):
    return int((sequence + 1) * percentage / 100) > int(sequence * percentage / 100)

def main():
    sys.stdout.reconfigure(line_buffering=True)
    args = parse_arguments()
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    counters = dict(attempts=0, sent=0, omitted=0, send_errors=0)
    boot = None
    server_ip = None
    next_refresh = 0
    total_attempts = 0
    final_ack = False

    def control(final=False):
        nonlocal boot, server_ip
        with telep.TelepConnection(args.host) as c:
            status = dict(part.split("=", 1) for part in c.request("GET_STATUS")[0][1].split(";"))
            new_boot = status["boot_id"]
            if boot is not None and new_boot != boot:
                print(json.dumps(dict(event="server_restart_previous_session", session=args.session, **counters)))
                args.session = uuid.uuid4().hex
                counters.update({k: 0 for k in counters})
            boot = new_boot
            answer = c.request("HELLO", args.id, args.session)[0]
            if answer[0] != "OK": raise RuntimeError("registration rejected: " + "|".join(answer))
            answer = c.request("REPORT", args.id, args.session, *counters.values(), int(final))[0]
            if answer[0] != "OK": raise RuntimeError("report rejected: " + "|".join(answer))
            server_ip = c.socket.getpeername()[0]
            return True

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        try:
            while total_attempts < args.count:
                if time.monotonic() >= next_refresh:
                    try:
                        control()
                        print(json.dumps(dict(event="control", host=args.host, resolved=server_ip, session=args.session, **counters)))
                        next_refresh = time.monotonic() + args.dns_refresh
                    except OSError as e:
                        # Pause attempts while DNS/control plane cannot establish the current session.
                        print("control unavailable: " + str(e), file=sys.stderr)
                        time.sleep(1)
                        continue
                seq = counters["attempts"]
                data = telep.encode_message("TELEMETRY", args.id, args.session, seq,
                    telep.encode_measurements(take_measurements(args.forced, args.fail_status)))
                if omitted_by_drop(seq, args.drop):
                    counters["omitted"] += 1
                else:
                    try:
                        if udp.sendto(data, (server_ip, telep.UDP_PORT)) != len(data): raise OSError("short UDP send")
                        counters["sent"] += 1
                    except OSError as e:
                        counters["send_errors"] += 1
                        next_refresh = 0
                        print(str(e), file=sys.stderr)
                counters["attempts"] += 1
                total_attempts += 1
                time.sleep(args.interval)
        except KeyboardInterrupt:
            pass
        finally:
            for _ in range(3):
                try:
                    final_ack = control(final=True)
                    break
                except (OSError, RuntimeError) as e:
                    print("final report unavailable: " + str(e), file=sys.stderr)
                    time.sleep(0.2)
            print(json.dumps(dict(event="final", id=args.id, session=args.session,
                                  final_ack=final_ack, **counters)))
    return 0 if final_ack else 2

if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
