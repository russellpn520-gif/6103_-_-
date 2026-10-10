#!/usr/bin/env python3
"""
bridge.py - a UDP client of the flight server with a browser front end.

The browser cannot send UDP, so this program runs on the SAME computer as the
browser and sits between the two:

    browser  --HTTP (localhost)-->  bridge.py  ==UDP (network)==>  C++ flight server

Towards the server the bridge is an ordinary client of the project protocol:
it marshals every request by hand into the byte format defined in
include/common/message_serializer.h (big-endian 32-bit integers, length-
prefixed strings, IEEE-754 floats), retransmits after a timeout, and
recognises replies by (message type, request id). Because it is written in
Python while the server is C++, it also covers the project's optional
"client and server in different languages" requirement.

Towards the browser it serves index.html and a small JSON API:

    POST /api/session            create (or re-attach to) a session
    GET  /api/state?token=...    session information and settings
    POST /api/settings           change semantics / timeout / retries / loss rate
    POST /api/call               invoke one of the six services
    GET  /api/events?token=...   Server-Sent Events: callbacks and the wire log

Every browser tab gets its own session with its own UDP socket, so the server
sees each tab as a separate client (distinct IP:port) and the request ids of
one tab never collide with those of another.

Only the Python standard library is used (Python 3.8 or newer).
"""

import argparse
import collections
import json
import math
import os
import queue
import random
import secrets
import socket
import struct
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

# ============================================================================
# Protocol constants (must match include/common/message_types.h)
# ============================================================================

MSG_QUERY_FLIGHT_REQUEST = 1
MSG_QUERY_FLIGHT_RESPONSE = 2
MSG_GET_FLIGHT_INFO_REQUEST = 3
MSG_GET_FLIGHT_INFO_RESPONSE = 4
MSG_RESERVE_SEATS_REQUEST = 5
MSG_RESERVE_SEATS_RESPONSE = 6
MSG_REGISTER_MONITOR_REQUEST = 7
MSG_REGISTER_MONITOR_RESPONSE = 8
MSG_CALLBACK_UPDATE = 9
MSG_GET_SEAT_COUNT_REQUEST = 10
MSG_GET_SEAT_COUNT_RESPONSE = 11
MSG_CANCEL_RESERVATION_REQUEST = 12
MSG_CANCEL_RESERVATION_RESPONSE = 13

TYPE_NAMES = {
    1: "QUERY_FLIGHT_REQUEST",
    2: "QUERY_FLIGHT_RESPONSE",
    3: "GET_FLIGHT_INFO_REQUEST",
    4: "GET_FLIGHT_INFO_RESPONSE",
    5: "RESERVE_SEATS_REQUEST",
    6: "RESERVE_SEATS_RESPONSE",
    7: "REGISTER_MONITOR_REQUEST",
    8: "REGISTER_MONITOR_RESPONSE",
    9: "CALLBACK_UPDATE",
    10: "GET_SEAT_COUNT_REQUEST",
    11: "GET_SEAT_COUNT_RESPONSE",
    12: "CANCEL_RESERVATION_REQUEST",
    13: "CANCEL_RESERVATION_RESPONSE",
}

ERR_SUCCESS = 0
ERR_INVALID_INPUT = 1
ERR_FLIGHT_NOT_FOUND = 2
ERR_NO_FLIGHTS_FOUND = 3
ERR_INSUFFICIENT_SEATS = 4
ERR_INVALID_SEAT_COUNT = 5
ERR_INVALID_MONITOR_INTERVAL = 6
ERR_CANCEL_EXCEEDS_RESERVED = 7
ERR_SERVER_ERROR = 99
# Produced locally by the client, never sent on the wire
ERR_NO_REPLY = 100
ERR_MALFORMED_REPLY = 101

ERROR_MESSAGES = {
    ERR_SUCCESS: "Success",
    ERR_INVALID_INPUT: "Invalid input",
    ERR_FLIGHT_NOT_FOUND: "Flight not found",
    ERR_NO_FLIGHTS_FOUND: "No flights found",
    ERR_INSUFFICIENT_SEATS: "Insufficient seats",
    ERR_INVALID_SEAT_COUNT: "Invalid seat count (must be a positive number)",
    ERR_INVALID_MONITOR_INTERVAL: "Invalid monitor interval (must be 1-3600 seconds)",
    ERR_CANCEL_EXCEEDS_RESERVED: "Cannot cancel more seats than are currently reserved",
    ERR_SERVER_ERROR: "Server error",
    ERR_NO_REPLY: "No reply from server (request/reply lost or server unreachable)",
    ERR_MALFORMED_REPLY: "Malformed reply from server",
}

SEMANTICS = ("maybe", "at-least-once", "at-most-once")

HEADER = struct.Struct("!III")      # message_type, request_id, message_length
HEADER_SIZE = HEADER.size           # 12 bytes
U32 = struct.Struct("!I")
I32 = struct.Struct("!i")
F32 = struct.Struct("!f")
MAX_DATAGRAM = 65535
MAX_UDP_PAYLOAD = 65507   # largest IPv4 UDP payload

# The server keeps replies in its history for 60 s; the whole retransmission
# window of one request must stay well below that.
MAX_RETRANSMISSION_WINDOW_MS = 30000


class ProtocolError(Exception):
    """A datagram does not have the expected layout."""


# ============================================================================
# Marshalling
# ============================================================================

class Writer:
    """Builds a message body field by field."""

    def __init__(self):
        self.buf = bytearray()

    def u32(self, value):
        self.buf += U32.pack(value)
        return self

    def i32(self, value):
        self.buf += I32.pack(value)
        return self

    def f32(self, value):
        self.buf += F32.pack(value)
        return self

    def string(self, text):
        # uint32 byte length, then the UTF-8 bytes (no terminator)
        data = text.encode("utf-8")
        self.buf += U32.pack(len(data)) + data
        return self

    def bytes(self):
        return bytes(self.buf)


class Reader:
    """Reads fields from a message, raising ProtocolError on underflow."""

    def __init__(self, data, offset=0):
        self.data = data
        self.offset = offset

    def _take(self, size, what):
        if self.offset + size > len(self.data):
            raise ProtocolError("Buffer underflow in " + what)
        chunk = self.data[self.offset:self.offset + size]
        self.offset += size
        return chunk

    def u32(self):
        return U32.unpack(self._take(4, "readUint32"))[0]

    def i32(self):
        return I32.unpack(self._take(4, "readInt32"))[0]

    def f32(self):
        return F32.unpack(self._take(4, "readFloat"))[0]

    def string(self):
        length = self.u32()
        return self._take(length, "readString").decode("utf-8", errors="replace")

    def time(self):
        year, month, day, hour, minute = (self.i32() for _ in range(5))
        return "%04d-%02d-%02d %02d:%02d" % (year, month, day, hour, minute)


def build_message(message_type, request_id, body=b""):
    """Prepend the 12-byte header; message_length is the total datagram size."""
    return HEADER.pack(message_type, request_id, HEADER_SIZE + len(body)) + body


def parse_header(data):
    """Return (message_type, request_id, message_length); validates the length field."""
    if len(data) < HEADER_SIZE:
        raise ProtocolError("Datagram shorter than a message header")
    message_type, request_id, message_length = HEADER.unpack_from(data)
    if message_length != len(data):
        raise ProtocolError("Length field %d does not match datagram size %d" % (message_length, len(data)))
    return message_type, request_id, message_length


def _reply_reader(data, expected_type):
    message_type, _, _ = parse_header(data)
    if message_type != expected_type:
        raise ProtocolError("Unexpected message type %s" % TYPE_NAMES.get(message_type, message_type))
    return Reader(data, HEADER_SIZE)


# --- reply parsers: return a dict with at least "status" --------------------

def parse_query_flight_response(data):
    r = _reply_reader(data, MSG_QUERY_FLIGHT_RESPONSE)
    status = r.i32()
    flight_ids = []
    if status == ERR_SUCCESS:
        count = r.u32()
        if count > (len(data) - r.offset) // 4:
            raise ProtocolError("Flight id count exceeds message size")
        flight_ids = [r.i32() for _ in range(count)]
    return {"status": status, "flight_ids": flight_ids}


def parse_get_flight_info_response(data):
    r = _reply_reader(data, MSG_GET_FLIGHT_INFO_RESPONSE)
    status = r.i32()
    result = {"status": status}
    if status == ERR_SUCCESS:
        flight = {
            "flight_id": r.i32(),
            "source": r.string(),
            "destination": r.string(),
            "departure_time": r.time(),
            "airfare": r.f32(),
            "seat_availability": r.i32(),
        }
        if not math.isfinite(flight["airfare"]):
            raise ProtocolError("Airfare is not a finite number")   # NaN/Inf is not valid JSON either
        flight["airfare"] = round(flight["airfare"], 2)
        result["flight"] = flight
    return result


def parse_reserve_seats_response(data):
    r = _reply_reader(data, MSG_RESERVE_SEATS_RESPONSE)
    return {"status": r.i32(), "remaining_seats": r.i32()}


def parse_register_monitor_response(data):
    r = _reply_reader(data, MSG_REGISTER_MONITOR_RESPONSE)
    return {"status": r.i32()}


def parse_get_seat_count_response(data):
    r = _reply_reader(data, MSG_GET_SEAT_COUNT_RESPONSE)
    return {"status": r.i32(), "seat_count": r.i32()}


def parse_cancel_reservation_response(data):
    r = _reply_reader(data, MSG_CANCEL_RESERVATION_RESPONSE)
    return {"status": r.i32(), "seat_availability": r.i32()}


def parse_callback_update(data):
    r = _reply_reader(data, MSG_CALLBACK_UPDATE)
    return {"flight_id": r.i32(), "seat_availability": r.i32(), "remaining_seconds": r.i32()}


# --- the six services -------------------------------------------------------
# name -> (request type, reply type, body builder(args) -> bytes, reply parser, idempotent)

def _int_arg(args, name, lo=-2**31, hi=2**31 - 1):
    value = args.get(name)
    if isinstance(value, bool) or not isinstance(value, int):
        try:
            value = int(str(value).strip())
        except (TypeError, ValueError):
            raise ValueError("%s must be a whole number" % name)
    if value < lo or value > hi:
        raise ValueError("%s is out of range" % name)
    return value


def _str_arg(args, name):
    value = args.get(name, "")
    if not isinstance(value, str):
        raise ValueError("%s must be text" % name)
    return value.strip()


OPERATIONS = {
    "query_flights": (
        MSG_QUERY_FLIGHT_REQUEST, MSG_QUERY_FLIGHT_RESPONSE,
        lambda a: Writer().string(_str_arg(a, "source")).string(_str_arg(a, "destination")).bytes(),
        parse_query_flight_response, True,
    ),
    "get_flight_info": (
        MSG_GET_FLIGHT_INFO_REQUEST, MSG_GET_FLIGHT_INFO_RESPONSE,
        lambda a: Writer().i32(_int_arg(a, "flight_id")).bytes(),
        parse_get_flight_info_response, True,
    ),
    "reserve_seats": (
        MSG_RESERVE_SEATS_REQUEST, MSG_RESERVE_SEATS_RESPONSE,
        lambda a: Writer().i32(_int_arg(a, "flight_id")).i32(_int_arg(a, "seat_count")).bytes(),
        parse_reserve_seats_response, False,
    ),
    # Registering twice only renews the interval, so a retransmission is harmless: treated as idempotent.
    "register_monitor": (
        MSG_REGISTER_MONITOR_REQUEST, MSG_REGISTER_MONITOR_RESPONSE,
        lambda a: Writer().i32(_int_arg(a, "flight_id")).i32(_int_arg(a, "interval_seconds")).bytes(),
        parse_register_monitor_response, True,
    ),
    "get_seat_count": (
        MSG_GET_SEAT_COUNT_REQUEST, MSG_GET_SEAT_COUNT_RESPONSE,
        lambda a: Writer().i32(_int_arg(a, "flight_id")).bytes(),
        parse_get_seat_count_response, True,
    ),
    "cancel_reservation": (
        MSG_CANCEL_RESERVATION_REQUEST, MSG_CANCEL_RESERVATION_RESPONSE,
        lambda a: Writer().i32(_int_arg(a, "flight_id")).i32(_int_arg(a, "seat_count")).bytes(),
        parse_cancel_reservation_response, False,
    ),
}


def hexdump(data):
    return " ".join("%02x" % b for b in data)


def describe_body(message_type, data):
    """Short human-readable summary of a datagram's body, for the wire log."""
    try:
        r = Reader(data, HEADER_SIZE)
        if message_type == MSG_QUERY_FLIGHT_REQUEST:
            return 'source="%s" destination="%s"' % (r.string(), r.string())
        if message_type in (MSG_GET_FLIGHT_INFO_REQUEST, MSG_GET_SEAT_COUNT_REQUEST):
            return "flight=%d" % r.i32()
        if message_type in (MSG_RESERVE_SEATS_REQUEST, MSG_CANCEL_RESERVATION_REQUEST):
            return "flight=%d seats=%d" % (r.i32(), r.i32())
        if message_type == MSG_REGISTER_MONITOR_REQUEST:
            return "flight=%d interval=%ds" % (r.i32(), r.i32())
        if message_type == MSG_CALLBACK_UPDATE:
            return "flight=%d seats=%d remaining=%ds" % (r.i32(), r.i32(), r.i32())
        if message_type == MSG_QUERY_FLIGHT_RESPONSE:
            p = parse_query_flight_response(data)
            return status_text(p["status"]) + (" flights=%s" % p["flight_ids"] if p["status"] == 0 else "")
        if message_type == MSG_GET_FLIGHT_INFO_RESPONSE:
            p = parse_get_flight_info_response(data)
            if p["status"] != 0:
                return status_text(p["status"])
            f = p["flight"]
            return "%s %s->%s %s fare=%.2f seats=%d" % (
                status_text(0), f["source"], f["destination"], f["departure_time"], f["airfare"], f["seat_availability"])
        if message_type in (MSG_RESERVE_SEATS_RESPONSE, MSG_GET_SEAT_COUNT_RESPONSE, MSG_CANCEL_RESERVATION_RESPONSE):
            status, seats = r.i32(), r.i32()
            return "%s seats=%d" % (status_text(status), seats)
        if message_type == MSG_REGISTER_MONITOR_RESPONSE:
            return status_text(r.i32())
    except (ProtocolError, struct.error):
        return "(malformed body)"
    return ""


def status_text(status):
    return ERROR_MESSAGES.get(status, "Unknown error %d" % status)


# ============================================================================
# Logging
# ============================================================================

_log_lock = threading.Lock()


def log(level, message):
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    with _log_lock:
        print("[%s] [%s] %s" % (stamp, level, message), flush=True)


# ============================================================================
# Session: one browser tab = one UDP client
# ============================================================================

class NoReply(Exception):
    pass


class Session:
    """One UDP socket, one request-id counter, one receive thread."""

    def __init__(self, token, server_addr, settings, seed):
        self.token = token
        self.server_addr = server_addr
        self.settings = dict(settings)
        self.seed = seed
        self.rng = random.Random(seed)
        self.created = time.time()
        self.last_used = self.created
        self.closed = False

        self.lock = threading.Lock()
        self.next_request_id = 1
        self.pending = {}           # request_id -> {"type": reply type, "event": Event, "reply": bytes}
        self.subscribers = []       # queues of connected SSE streams
        self.recent = collections.deque(maxlen=400)   # replayed to a (re)connecting page
        self.monitors = {}          # flight_id -> expiry time (local copy, for the page)

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", 0))      # OS-chosen port: this is the client's address
        self.sock.settimeout(0.5)           # lets the receive thread notice close()
        self.local_port = self.sock.getsockname()[1]

        self.thread = threading.Thread(target=self._receive_loop, name="udp-" + token, daemon=True)
        self.thread.start()

    # ---- bookkeeping -------------------------------------------------------

    def info(self):
        now = time.time()
        with self.lock:
            monitors = [
                {"flight_id": fid, "remaining_seconds": max(0, int(round(expiry - now)))}
                for fid, expiry in self.monitors.items() if expiry > now
            ]
            next_id = self.next_request_id
        return {
            "token": self.token,
            "local_port": self.local_port,
            "server": "%s:%d" % self.server_addr,
            "settings": dict(self.settings),
            "seed": self.seed,
            "next_request_id": next_id,
            "monitors": monitors,
        }

    def close(self):
        self.closed = True
        try:
            self.sock.close()
        except OSError:
            pass

    def _emit(self, event):
        event["time"] = time.strftime("%H:%M:%S")
        event["t"] = time.time()
        with self.lock:
            self.recent.append(event)
            subscribers = list(self.subscribers)
        for q in subscribers:
            q.put(event)

    def _log_wire(self, direction, data, note, **extra):
        """One wire-log entry (also printed to the console)."""
        try:
            message_type, request_id, _ = HEADER.unpack_from(data)
        except struct.error:
            message_type, request_id = 0, 0
        entry = {
            "kind": "wire",
            "direction": direction,               # "send", "recv", "drop"
            "type": message_type,
            "type_name": TYPE_NAMES.get(message_type, "UNKNOWN"),
            "request_id": request_id,
            "length": len(data),
            "hex": hexdump(data),
            "summary": describe_body(message_type, data),
            "note": note,
        }
        entry.update(extra)
        self._emit(entry)
        arrow = {"send": "->", "recv": "<-", "drop": "x "}.get(direction, "  ")
        line = "[%d] %s %s #%d (%d bytes) %s %s" % (
            self.local_port, arrow, entry["type_name"], request_id, len(data), entry["summary"], note)
        log("INFO", line.rstrip())

    def _log_event(self, level, text):
        self._emit({"kind": "log", "level": level, "text": text})
        log(level, "[%d] %s" % (self.local_port, text))

    # ---- receiving ---------------------------------------------------------

    def _receive_loop(self):
        while not self.closed:
            try:
                data, addr = self.sock.recvfrom(MAX_DATAGRAM)
            except socket.timeout:
                continue
            except ConnectionResetError:
                # Windows reports an ICMP "port unreachable" for an earlier
                # sendto() as an error on the next recvfrom(); UDP has no
                # connections, so this just means the server was not listening.
                continue
            except OSError:
                if self.closed:
                    break
                continue
            try:
                self._dispatch(data, addr)
            except Exception as e:  # never let the receive thread die
                log("ERROR", "[%d] Error while handling a datagram: %r" % (self.local_port, e))

    def _dispatch(self, data, addr):
        try:
            message_type, request_id, _ = parse_header(data)
        except ProtocolError as e:
            self._log_event("WARN", "Dropped %d-byte datagram from %s:%d: %s" % (len(data), addr[0], addr[1], e))
            return

        if message_type == MSG_CALLBACK_UPDATE:
            # Initiated by the server: not a reply to anything we are waiting for.
            try:
                update = parse_callback_update(data)
            except ProtocolError as e:
                self._log_event("WARN", "Malformed callback: %s" % e)
                return
            self._log_wire("recv", data, "callback from server")
            with self.lock:
                self.monitors[update["flight_id"]] = time.time() + update["remaining_seconds"]
            update["kind"] = "callback"
            self._emit(update)
            return

        with self.lock:
            pending = self.pending.get(request_id)
            if pending is not None and pending["type"] == message_type and pending["reply"] is None:
                pending["reply"] = data
                pending["event"].set()
                matched = True
            else:
                matched = False
        if not matched:
            # A late or duplicate reply (e.g. the original reply arriving after
            # we already accepted a retransmission's reply), or something
            # unexpected: discard it.
            self._log_wire("recv", data, "discarded: no request is waiting for this reply")

    # ---- sending with timeout and retransmission ---------------------------

    def _should_lose(self):
        rate = self.settings["loss_rate"]
        if rate <= 0:
            return False
        if rate >= 1:
            return True
        return self.rng.random() < rate

    def invoke(self, op_name, args):
        """Execute one service call. Returns a result dict for the browser."""
        if op_name not in OPERATIONS:
            raise ValueError("Unknown operation: " + op_name)
        req_type, reply_type, build_body, parse_reply, idempotent = OPERATIONS[op_name]
        body = build_body(args)
        if HEADER_SIZE + len(body) > MAX_UDP_PAYLOAD:
            raise ValueError("Request too large for one UDP datagram (%d bytes)" % (HEADER_SIZE + len(body)))

        with self.lock:
            request_id = self.next_request_id
            self.next_request_id += 1
            self.last_used = time.time()
            event = threading.Event()
            pending = {"type": reply_type, "event": event, "reply": None}
            self.pending[request_id] = pending

        request = build_message(req_type, request_id, body)
        semantics = self.settings["semantics"]
        timeout_s = self.settings["timeout_ms"] / 1000.0
        # Under maybe semantics the request is sent exactly once. Otherwise the
        # SAME bytes (same request id) are resent each time the timeout expires;
        # the unchanged id is what lets an at-most-once server detect the duplicate.
        attempts = 1 if semantics == "maybe" else 1 + self.settings["retries"]
        started = time.time()
        reply = None

        try:
            for attempt in range(1, attempts + 1):
                if attempt > 1:
                    self._log_event("WARN", "Timeout: no reply to %s #%d within %d ms, retransmitting (attempt %d/%d)" % (
                        TYPE_NAMES[req_type], request_id, self.settings["timeout_ms"], attempt, attempts))
                if self._should_lose():
                    self._log_wire("drop", request, "[simulated loss] request dropped before sending",
                                   attempt=attempt, attempts=attempts)
                else:
                    self.sock.sendto(request, self.server_addr)
                    self._log_wire("send", request, "retransmission" if attempt > 1 else "",
                                   attempt=attempt, attempts=attempts)
                if event.wait(timeout_s):
                    reply = pending["reply"]
                    break
        finally:
            with self.lock:
                self.pending.pop(request_id, None)
                if reply is None and pending["reply"] is not None:
                    reply = pending["reply"]     # arrived just after the last timeout: still ours

        elapsed_ms = int((time.time() - started) * 1000)
        result = {
            "op": op_name,
            "request_id": request_id,
            "attempts": attempt,
            "elapsed_ms": elapsed_ms,
            "idempotent": idempotent,
            "semantics": semantics,
        }

        if reply is None:
            if semantics == "maybe":
                self._log_event("ERROR", "No reply to %s #%d (maybe semantics: not retransmitted)" % (
                    TYPE_NAMES[req_type], request_id))
            else:
                self._log_event("ERROR", "No reply to %s #%d after %d attempt(s), giving up" % (
                    TYPE_NAMES[req_type], request_id, attempts))
            result["status"] = ERR_NO_REPLY
            result["message"] = status_text(ERR_NO_REPLY)
            if not idempotent:
                result["message"] += ". The outcome is UNKNOWN: the server may have executed the operation."
            return result

        self._log_wire("recv", reply, "reply after %d attempt(s), %d ms" % (attempt, elapsed_ms), attempt=attempt)
        try:
            parsed = parse_reply(reply)
        except (ProtocolError, struct.error) as e:
            self._log_event("ERROR", "Malformed reply: %s" % e)
            result["status"] = ERR_MALFORMED_REPLY
            result["message"] = status_text(ERR_MALFORMED_REPLY)
            return result

        result.update(parsed)
        result["message"] = status_text(parsed["status"])
        if op_name == "register_monitor" and parsed["status"] == ERR_SUCCESS:
            interval = _int_arg(args, "interval_seconds")
            with self.lock:
                self.monitors[_int_arg(args, "flight_id")] = time.time() + interval
            result["interval_seconds"] = interval
        return result

    # ---- SSE subscriptions -------------------------------------------------

    def subscribe(self):
        q = queue.Queue()
        with self.lock:
            self.subscribers.append(q)
            backlog = list(self.recent)
        return q, backlog

    def unsubscribe(self, q):
        with self.lock:
            if q in self.subscribers:
                self.subscribers.remove(q)


# ============================================================================
# Bridge: the set of sessions plus the defaults
# ============================================================================

class Bridge:
    SESSION_IDLE_SECONDS = 6 * 3600

    def __init__(self, server_addr, settings, seed):
        self.server_addr = server_addr
        self.settings = settings
        self.base_seed = seed
        self.sessions = {}
        self.lock = threading.Lock()

    def get_or_create(self, token):
        with self.lock:
            self._expire_idle()
            if token and token in self.sessions:
                session = self.sessions[token]
                session.last_used = time.time()
                return session, False
            token = secrets.token_hex(6)
            # Each session gets its own deterministic loss sequence.
            seed = (self.base_seed + len(self.sessions) * 7919) & 0xFFFFFFFF
            session = Session(token, self.server_addr, self.settings, seed)
            self.sessions[token] = session
            log("INFO", "New session %s: UDP port %d -> server %s:%d (%s)" % (
                token, session.local_port, self.server_addr[0], self.server_addr[1], session.settings["semantics"]))
            return session, True

    def get(self, token):
        with self.lock:
            return self.sessions.get(token)

    def _expire_idle(self):
        now = time.time()
        for token, session in list(self.sessions.items()):
            if now - session.last_used > self.SESSION_IDLE_SECONDS and not session.subscribers:
                session.close()
                del self.sessions[token]


def validate_settings(current, changes):
    """Return a new settings dict, or raise ValueError."""
    new = dict(current)
    if "semantics" in changes:
        if changes["semantics"] not in SEMANTICS:
            raise ValueError("semantics must be one of " + ", ".join(SEMANTICS))
        new["semantics"] = changes["semantics"]
    if "timeout_ms" in changes:
        new["timeout_ms"] = _int_arg(changes, "timeout_ms", 100, 10000)
    if "retries" in changes:
        new["retries"] = _int_arg(changes, "retries", 0, 20)
    if "loss_rate" in changes:
        try:
            rate = float(changes["loss_rate"])
        except (TypeError, ValueError):
            raise ValueError("loss_rate must be a number")
        if not 0.0 <= rate <= 1.0:
            raise ValueError("loss_rate must be between 0 and 1")
        new["loss_rate"] = rate
    window = new["timeout_ms"] * (new["retries"] + 1)
    if window > MAX_RETRANSMISSION_WINDOW_MS:
        raise ValueError("timeout x (retries + 1) must not exceed %d ms (the server's reply history lasts 60 s)"
                         % MAX_RETRANSMISSION_WINDOW_MS)
    return new


# ============================================================================
# HTTP front end
# ============================================================================

WEB_DIR = os.path.dirname(os.path.abspath(__file__))


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "FlightBridge/1.0"

    # ---- helpers -----------------------------------------------------------

    @property
    def bridge(self):
        return self.server.bridge

    def log_message(self, fmt, *args):
        # Keep the console for the UDP wire log; HTTP chatter is not interesting.
        pass

    def send_json(self, payload, status=200):
        data = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def send_error_json(self, status, message):
        self.send_json({"error": message}, status)

    def read_json(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length > 1_000_000:
            raise ValueError("Request body too large")
        raw = self.rfile.read(length) if length else b""
        if not raw:
            return {}
        payload = json.loads(raw.decode("utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("JSON object expected")
        for key in ("token", "op"):
            if payload.get(key) is not None and not isinstance(payload[key], str):
                raise ValueError("%s must be a string" % key)
        return payload

    def session_from(self, token):
        session = self.bridge.get(token) if isinstance(token, str) and token else None
        if session is None:
            self.send_error_json(404, "Unknown session; reload the page")
        return session

    # The browser may close the connection while a call is still waiting for
    # the UDP reply (tab closed or reloaded). That is not an error worth a
    # traceback on the console.
    def handle_one_request(self):
        try:
            super().handle_one_request()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, socket.timeout):
            self.close_connection = True

    # ---- routes ------------------------------------------------------------

    def do_GET(self):
        url = urlparse(self.path)
        if url.path in ("/", "/index.html"):
            return self.serve_file("index.html", "text/html; charset=utf-8")
        query = parse_qs(url.query)
        token = (query.get("token") or [""])[0]
        if url.path == "/api/state":
            session = self.session_from(token)
            if session:
                self.send_json(session.info())
            return
        if url.path == "/api/events":
            session = self.session_from(token)
            if session:
                self.stream_events(session)
            return
        self.send_error_json(404, "Not found")

    def do_POST(self):
        url = urlparse(self.path)
        try:
            payload = self.read_json()
        except (ValueError, json.JSONDecodeError) as e:
            return self.send_error_json(400, "Bad request: %s" % e)

        if url.path == "/api/session":
            session, created = self.bridge.get_or_create(payload.get("token"))
            info = session.info()
            info["created"] = created
            return self.send_json(info)

        session = self.session_from(payload.get("token"))
        if session is None:
            return

        if url.path == "/api/settings":
            try:
                session.settings = validate_settings(session.settings, payload)
            except ValueError as e:
                return self.send_error_json(400, str(e))
            session._log_event("INFO", "Settings: semantics=%s timeout=%d ms retries=%d loss_rate=%.2f" % (
                session.settings["semantics"], session.settings["timeout_ms"],
                session.settings["retries"], session.settings["loss_rate"]))
            return self.send_json(session.info())

        if url.path == "/api/call":
            op = payload.get("op")
            args = payload.get("args") or {}
            if op not in OPERATIONS or not isinstance(args, dict):
                return self.send_error_json(400, "Unknown operation")
            try:
                result = session.invoke(op, args)
            except ValueError as e:
                return self.send_error_json(400, str(e))
            except (OSError, OverflowError) as e:
                return self.send_error_json(500, "Socket error: %s" % e)
            return self.send_json(result)

        self.send_error_json(404, "Not found")

    def serve_file(self, name, content_type):
        path = os.path.join(WEB_DIR, name)
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            return self.send_error_json(404, "%s not found next to bridge.py" % name)
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def stream_events(self, session):
        """Server-Sent Events: replay the recent log, then push live events."""
        q, backlog = session.subscribe()
        self.connection.settimeout(60)              # a peer that stops reading is dropped, not kept forever
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")      # body ends when the connection closes
        self.send_header("X-Accel-Buffering", "no")
        self.end_headers()
        try:
            self._sse("hello", session.info(), retry=2000)
            for event in backlog:
                replayed = dict(event)
                replayed["replay"] = True            # history, not a live event
                self._sse(event["kind"], replayed)
            while not session.closed:
                try:
                    event = q.get(timeout=15)
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")   # keep-alive; also detects a closed connection
                    continue
                self._sse(event["kind"], event)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, socket.timeout, OSError):
            pass
        finally:
            session.unsubscribe(q)
            self.close_connection = True

    def _sse(self, name, payload, retry=None):
        chunk = ""
        if retry is not None:
            chunk += "retry: %d\n" % retry
        chunk += "event: %s\ndata: %s\n\n" % (name, json.dumps(payload))
        self.wfile.write(chunk.encode("utf-8"))


# ============================================================================
# main
# ============================================================================

class BridgeServer(ThreadingHTTPServer):
    # http.server sets SO_REUSEADDR, which on Windows lets a second bridge bind
    # a port that is already being listened on; the two would then share the
    # browser's connections. Keep it only where it means "skip TIME_WAIT".
    allow_reuse_address = sys.platform != "win32"
    daemon_threads = True


def port_number(text):
    try:
        value = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError("port must be a number")
    if not 1 <= value <= 65535:
        raise argparse.ArgumentTypeError("port must be between 1 and 65535")
    return value


def seed_number(text):
    try:
        value = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError("seed must be a number")
    if not 0 <= value <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("seed must be between 0 and 4294967295")
    return value


def ipv4_address(text):
    try:
        socket.inet_pton(socket.AF_INET, text)
    except (OSError, ValueError):
        raise argparse.ArgumentTypeError("an IPv4 address such as 192.168.1.10 is required")
    return text


def parse_args(argv):
    p = argparse.ArgumentParser(
        description="Browser front end for the UDP flight server (runs on the client computer).")
    p.add_argument("--server", type=ipv4_address, default="127.0.0.1",
                   help="flight server IPv4 address (default 127.0.0.1)")
    p.add_argument("--port", type=port_number, default=8080, help="flight server UDP port (default 8080)")
    p.add_argument("--http-port", type=port_number, default=8000,
                   help="local HTTP port for the browser (default 8000)")
    p.add_argument("--bind", type=ipv4_address, default="127.0.0.1",
                   help="address the HTTP server listens on (default 127.0.0.1: this computer only)")
    p.add_argument("--semantics", default="at-most-once", choices=SEMANTICS,
                   help="default invocation semantics for new sessions (use the same as the server)")
    p.add_argument("--timeout-ms", type=int, default=2000, help="reply timeout before retransmitting (default 2000)")
    p.add_argument("--retries", type=int, default=5, help="retransmissions after the first attempt (default 5)")
    p.add_argument("--loss-rate", type=float, default=0.0, help="probability that a request is dropped (default 0)")
    p.add_argument("--seed", type=seed_number, default=None,
                   help="seed for the loss simulation (default: current time)")
    p.add_argument("--open", action="store_true", help="open the page in the default browser")
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    try:
        settings = validate_settings(
            {"semantics": "at-most-once", "timeout_ms": 2000, "retries": 5, "loss_rate": 0.0},
            {"semantics": args.semantics, "timeout_ms": args.timeout_ms,
             "retries": args.retries, "loss_rate": args.loss_rate})
    except ValueError as e:
        print("Invalid option: %s" % e, file=sys.stderr)
        return 1

    seed = args.seed if args.seed is not None else int(time.time()) & 0xFFFFFFFF
    bridge = Bridge((args.server, args.port), settings, seed)

    try:
        httpd = BridgeServer((args.bind, args.http_port), Handler)
    except (OSError, OverflowError) as e:
        print("Cannot listen on %s:%d: %s (is another bridge already running?)" % (args.bind, args.http_port, e),
              file=sys.stderr)
        return 1
    httpd.bridge = bridge

    url = "http://%s:%d/" % ("localhost" if args.bind in ("127.0.0.1", "0.0.0.0") else args.bind, args.http_port)
    log("INFO", "Flight bridge: browser page at %s" % url)
    log("INFO", "UDP flight server: %s:%d" % (args.server, args.port))
    log("INFO", "Defaults for new sessions: semantics=%s timeout=%d ms retries=%d loss_rate=%.2f seed=%d" % (
        settings["semantics"], settings["timeout_ms"], settings["retries"], settings["loss_rate"], seed))
    log("INFO", "Each browser tab becomes its own UDP client; press Ctrl+C to stop")
    if args.open:
        webbrowser.open(url)

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        log("INFO", "Stopping")
    finally:
        httpd.server_close()
        for session in list(bridge.sessions.values()):
            session.close()
    return 0


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
