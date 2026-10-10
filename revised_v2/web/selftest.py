#!/usr/bin/env python3
"""
selftest.py - end-to-end check of bridge.py against the C++ flight server.

It starts the bridge in-process on a free port, creates two sessions (as two
browser tabs would) and drives them through the HTTP API:

  1. interoperability of all six services (Python marshalling <-> C++ server),
  2. one session watches flight 1001 while the other books seats: the callback
     must arrive through the first session's event stream, and the second
     session's request #1 must not be mistaken for the first session's #1,
  3. input validation errors from the server,
  4. (with --server-exe) the invocation-semantics experiment: the server is
     started with simulated reply loss under at-least-once, then at-most-once.

Usage:
    python3 selftest.py                         # server already running on 127.0.0.1:8080
    python3 selftest.py --server-exe ../server  # also runs the semantics experiment
"""

import argparse
import json
import os
import subprocess
import sys
import threading
import time
import urllib.request
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge  # noqa: E402

FLIGHT = 1001
INITIAL_SEATS = 120
passed = 0
failed = 0


def check(condition, text):
    global passed, failed
    if condition:
        passed += 1
        print("  [PASS] " + text)
    else:
        failed += 1
        print("  [FAIL] " + text)
    return condition


class Tab:
    """A fake browser tab: one session plus an SSE listener thread."""

    def __init__(self, base):
        self.base = base
        self.events = []
        info = self.post("/api/session", {})
        self.token = info["token"]
        self.port = info["local_port"]
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._listen, daemon=True)
        self.thread.start()
        time.sleep(0.2)

    def post(self, path, payload):
        payload = dict(payload)
        if hasattr(self, "token"):
            payload.setdefault("token", self.token)
        req = urllib.request.Request(self.base + path, data=json.dumps(payload).encode(),
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                return json.loads(resp.read().decode())
        except urllib.error.HTTPError as e:
            return {"http_status": e.code, "error": json.loads(e.read().decode()).get("error")}

    def call(self, op, **args):
        return self.post("/api/call", {"op": op, "args": args})

    def settings(self, **changes):
        return self.post("/api/settings", changes)

    def _listen(self):
        req = urllib.request.Request(self.base + "/api/events?token=" + self.token)
        with urllib.request.urlopen(req, timeout=120) as resp:
            name = None
            for raw in resp:
                line = raw.decode().rstrip("\n")
                if line.startswith("event: "):
                    name = line[7:]
                elif line.startswith("data: ") and name:
                    with self.lock:
                        self.events.append((name, json.loads(line[6:])))
                    name = None

    def wait_for(self, name, predicate, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.lock:
                for n, e in self.events:
                    if n == name and predicate(e):
                        return e
            time.sleep(0.05)
        return None

    def count(self, name, predicate=lambda e: True):
        with self.lock:
            return sum(1 for n, e in self.events if n == name and predicate(e))


def start_bridge(server_ip, server_port, seed):
    settings = {"semantics": "at-most-once", "timeout_ms": 2000, "retries": 5, "loss_rate": 0.0}
    b = bridge.Bridge((server_ip, server_port), settings, seed)
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), bridge.Handler)
    httpd.daemon_threads = True
    httpd.bridge = b
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, "http://127.0.0.1:%d" % httpd.server_address[1]


def start_server(exe, port, semantics, loss_rate, seed):
    proc = subprocess.Popen([exe, "--port=%d" % port, "--semantics=" + semantics,
                             "--loss-rate=%s" % loss_rate, "--seed=%d" % seed],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    # Drain the server's log continuously: if the pipe filled up, the server
    # would block on its next log line and stop answering requests.
    proc.log_lines = []
    def drain():
        for line in proc.stdout:
            proc.log_lines.append(line.rstrip("\n"))
    proc.drain_thread = threading.Thread(target=drain, daemon=True)
    proc.drain_thread.start()
    time.sleep(1.0)
    if proc.poll() is not None:
        print("\n".join(proc.log_lines))
        sys.exit("server exited immediately (port %d in use?)" % port)
    return proc


def probe_server(base):
    """One quick query so a missing server is reported at once instead of after minutes of timeouts."""
    tab = Tab(base)
    tab.settings(timeout_ms=300, retries=3)
    r = tab.call("get_seat_count", flight_id=FLIGHT)
    if r.get("status") == bridge.ERR_NO_REPLY:
        sys.exit("no flight server is answering at %s; start it first (or pass --server-exe)" % tab.post("/api/state", {}).get("server"))


def stop_server(proc):
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    proc.drain_thread.join(timeout=2)
    return "\n".join(proc.log_lines)


# ---------------------------------------------------------------------------

def test_services(base):
    print("\n1. Six services through two tabs (server must be fresh: flight %d has %d seats)" % (FLIGHT, INITIAL_SEATS))
    a, b = Tab(base), Tab(base)
    check(a.port != b.port, "two tabs use two different UDP ports (%d, %d)" % (a.port, b.port))

    r = a.call("register_monitor", flight_id=FLIGHT, interval_seconds=8)
    check(r.get("status") == 0 and r["request_id"] == 1, "tab A: register monitor on %d is request #1 -> %s" % (FLIGHT, r.get("message")))

    r = b.call("reserve_seats", flight_id=FLIGHT, seat_count=2)
    check(r.get("status") == 0 and r.get("remaining_seats") == INITIAL_SEATS - 2 and r["request_id"] == 1,
          "tab B: reserve 2 seats is ALSO request #1 and is executed (remaining %s)" % r.get("remaining_seats"))

    cb = a.wait_for("callback", lambda e: e["flight_id"] == FLIGHT and e["seat_availability"] == INITIAL_SEATS - 2, 3)
    check(cb is not None, "tab A received the callback: flight %d now %s seats" % (FLIGHT, cb and cb["seat_availability"]))

    r = b.call("query_flights", source="Beijing", destination="Shanghai")
    check(r.get("status") == 0 and r.get("flight_ids") == [1001, 1002, 1003], "query Beijing->Shanghai: %s" % r.get("flight_ids"))

    r = b.call("get_flight_info", flight_id=FLIGHT)
    f = r.get("flight") or {}
    check(r.get("status") == 0 and f.get("source") == "Beijing" and f.get("destination") == "Shanghai"
          and f.get("departure_time") == "2026-10-20 08:30" and abs(f.get("airfare", 0) - 800.5) < 0.01
          and f.get("seat_availability") == INITIAL_SEATS - 2,
          "flight info: %s -> %s, %s, fare %s, seats %s" % (f.get("source"), f.get("destination"),
                                                            f.get("departure_time"), f.get("airfare"), f.get("seat_availability")))

    r = b.call("get_seat_count", flight_id=FLIGHT)
    check(r.get("status") == 0 and r.get("seat_count") == INITIAL_SEATS - 2, "seat count: %s" % r.get("seat_count"))

    r = b.call("cancel_reservation", flight_id=FLIGHT, seat_count=2)
    check(r.get("status") == 0 and r.get("seat_availability") == INITIAL_SEATS, "cancel 2 seats: %s seats" % r.get("seat_availability"))
    cb = a.wait_for("callback", lambda e: e["seat_availability"] == INITIAL_SEATS, 3)
    check(cb is not None, "tab A received the callback for the cancellation too")

    print("\n2. Validation errors returned by the server")
    cases = [
        ("reserve_seats", dict(flight_id=9999, seat_count=1), bridge.ERR_FLIGHT_NOT_FOUND),
        ("reserve_seats", dict(flight_id=FLIGHT, seat_count=-5), bridge.ERR_INVALID_SEAT_COUNT),
        ("reserve_seats", dict(flight_id=FLIGHT, seat_count=500), bridge.ERR_INSUFFICIENT_SEATS),
        ("cancel_reservation", dict(flight_id=FLIGHT, seat_count=1), bridge.ERR_CANCEL_EXCEEDS_RESERVED),
        ("register_monitor", dict(flight_id=FLIGHT, interval_seconds=0), bridge.ERR_INVALID_MONITOR_INTERVAL),
        ("register_monitor", dict(flight_id=4242, interval_seconds=5), bridge.ERR_FLIGHT_NOT_FOUND),
        ("query_flights", dict(source="Kuala Lumpur", destination="Singapore"), bridge.ERR_NO_FLIGHTS_FOUND),
        ("query_flights", dict(source="", destination="Singapore"), bridge.ERR_INVALID_INPUT),
        ("get_flight_info", dict(flight_id=1), bridge.ERR_FLIGHT_NOT_FOUND),
    ]
    for op, args, expected in cases:
        r = b.call(op, **args)
        check(r.get("status") == expected, "%s %s -> %s" % (op, args, r.get("message")))

    print("\n3. Bridge-side checks")
    r = b.settings(timeout_ms=10000, retries=5)
    check(r.get("http_status") == 400, "settings with a 60 s retransmission window are rejected: %s" % r.get("error"))
    r = b.call("reserve_seats", flight_id="abc", seat_count=1)
    check(r.get("http_status") == 400, "non-numeric flight id is rejected before sending: %s" % r.get("error"))
    sends = b.count("wire", lambda e: e["direction"] == "send")
    recvs = b.count("wire", lambda e: e["direction"] == "recv")
    retrans = b.count("wire", lambda e: e["direction"] == "send" and e.get("attempt", 1) > 1)
    check(sends > 0 and recvs == sends and retrans == 0,
          "tab B wire log: %d sent, %d received, %d retransmissions without loss" % (sends, recvs, retrans))
    hello = b.wait_for("hello", lambda e: True, 1)
    check(hello is not None and hello["local_port"] == b.port, "event stream starts with the session's hello")


def test_semantics(exe, server_port, seed):
    print("\n4. Invocation semantics experiment (server reply loss 0.5, seed %d, 10 x reserve 1 seat)" % seed)
    print("   %-14s %9s %8s %15s %11s %12s" % ("semantics", "successes", "unknown", "retransmitted", "executed", "seats taken"))
    outcome = {}
    for sem in ("maybe", "at-least-once", "at-most-once"):
        server_sem = "at-least-once" if sem == "maybe" else sem
        proc = start_server(exe, server_port, server_sem, 0.5, seed)
        httpd, base = start_bridge("127.0.0.1", server_port, seed)
        tab = Tab(base)
        tab.settings(semantics=sem, timeout_ms=300, retries=8)
        results = [tab.call("reserve_seats", flight_id=FLIGHT, seat_count=1) for _ in range(10)]
        probe = Tab(base)
        probe.settings(semantics="at-least-once", timeout_ms=300, retries=8)
        final = probe.call("get_seat_count", flight_id=FLIGHT).get("seat_count")
        httpd.shutdown()
        log = stop_server(proc)

        ok = sum(1 for r in results if r.get("status") == 0)
        unknown = sum(1 for r in results if r.get("status") == bridge.ERR_NO_REPLY)
        retrans = tab.count("wire", lambda e: e["direction"] == "send" and e.get("attempt", 1) > 1)
        executed = sum(1 for line in log.splitlines() if "RESERVE_SEATS flight=%d" % FLIGHT in line and "Request #" in line)
        filtered = sum(1 for line in log.splitlines() if "NOT re-executed" in line)
        taken = INITIAL_SEATS - (final if final is not None else INITIAL_SEATS)
        print("   %-14s %9d %8d %15d %11d %12d" % (sem, ok, unknown, retrans, executed, taken))
        outcome[sem] = (ok, unknown, retrans, executed, filtered, taken)

    ok, unknown, _, _, _, taken = outcome["maybe"]
    check(unknown > 0 and taken > ok, "maybe: %d calls reported 'outcome unknown', yet %d seats were taken" % (unknown, taken))
    ok, _, retrans, executed, _, taken = outcome["at-least-once"]
    check(retrans > 0 and taken > 10 and executed == taken,
          "at-least-once: WRONG result, %d seats taken for 10 reservations (%d executions)" % (taken, executed))
    ok, _, retrans, executed, filtered, taken = outcome["at-most-once"]
    check(retrans > 0 and taken == 10 and executed == 10 and filtered > 0,
          "at-most-once: correct, exactly %d seats taken; server filtered %d duplicate(s) of %d retransmissions"
          % (taken, filtered, retrans))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--server", default="127.0.0.1")
    p.add_argument("--port", type=int, default=8080)
    p.add_argument("--server-exe", help="path to the server binary; enables the semantics experiment")
    p.add_argument("--server-port", type=int, default=9450,
                   help="first UDP port for servers started with --server-exe (default 9450; also uses +1)")
    p.add_argument("--seed", type=int, default=6103)
    args = p.parse_args()

    server_proc = None
    if args.server_exe:
        args.server = "127.0.0.1"
        args.port = args.server_port
        server_proc = start_server(args.server_exe, args.port, "at-most-once", 0.0, args.seed)

    httpd, base = start_bridge(args.server, args.port, args.seed)
    print("Bridge at %s -> server %s:%d" % (base, args.server, args.port))
    try:
        probe_server(base)
        test_services(base)
    finally:
        httpd.shutdown()
        if server_proc:
            stop_server(server_proc)

    if args.server_exe:
        test_semantics(args.server_exe, args.port + 1, args.seed)

    print("\nPassed: %d   Failed: %d" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
