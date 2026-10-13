# Changes in revised_v2

This folder is a self-contained revision of the original code at the repository root. None of the original files (sources, documents, scripts) were overwritten or modified. The revision was made against the project manual (Course.Project.for2026a.pdf) and the lectures.

## 1. Main problems and fixes

### 1.1 Invocation semantics were labelled the wrong way round (most important)

As defined in the lecture on remote invocation:

| Semantics | Client retransmits | Server filters duplicates | A duplicate request is |
|---|---|---|---|
| maybe | no | — | — |
| at-least-once | yes | no | executed again |
| at-most-once | yes | yes | answered with the stored reply, not executed again |

In the original version the default mode (no retransmission, no filtering) was actually *maybe*, and the option `--at-least-once` (client retransmits and server filters duplicates) was actually *at-most-once*. The conclusion in the original `docs/DEV_Log.md` that at-least-once suits non-idempotent operations is therefore reversed.

The revision:

- replaces the option with `--semantics=at-least-once|at-most-once` (the client also accepts `maybe` as a baseline); both ends use the same value, at-most-once by default;
- retransmits with the **same request id** under both semantics; only the server differs: under at-most-once it filters duplicates and resends the stored reply, under at-least-once it executes every copy.

### 1.2 Duplicates were detected by request id only

Every client numbers its requests from 1. With the request id alone, the monitoring client's request #1 and the booking client's request #1 on another computer collided in the demonstration scenario: the reservation was treated as a duplicate and never executed, and the booking client failed while reading a reply of the wrong type.

The revision identifies a request by (client IP, client port, request id), that is, client identifier plus request identifier.

### 1.3 The required comparison experiment was missing

The project asks to show that at-least-once gives wrong results for non-idempotent operations while at-most-once is correct for all operations. The original `test_semantics.sh` did not perform this experiment.

The rewritten [tools/test_semantics.sh](tools/test_semantics.sh) makes the server drop each reply with probability 0.5. Results with seed 6103 and twenty one-seat reservations:

| Semantics | Successes | Errors | Retransmissions | Executions at the server | Seats taken |
|---|---|---|---|---|---|
| maybe | 9 | 11 | 0 | 20 | 20 |
| at-least-once | 20 | 0 | 15 | 35 | **35 (wrong)** |
| at-most-once | 20 | 0 | 15 | 20 | **20 (correct)** |

- maybe: the client believes 11 reservations failed, although the server executed all of them.
- at-least-once: retransmissions after lost replies are executed again, taking 15 extra seats.
- at-most-once: duplicates are answered from the history, and every reservation is executed once.
- The idempotent seat-count query is executed again 15 times under at-least-once, and all 20 answers are still correct.
- Demonstration scenario (one client monitors, the other reserves, both start with request #1): the reservation is executed and the monitoring client receives the callback.

The server and client logs of every run are saved in `experiment_logs/`.

## 2. Other changes

- **Input validation**: reserving on an unknown flight returns "Flight not found" (the original returned "Insufficient seats"); the seat count must be positive (the original added seats for a negative reservation); a cancellation cannot return more seats than are reserved (the original could create seats); the monitor interval is limited to 1-3600 seconds.
- **Robustness**: the server catches unmarshalling errors, so a malformed datagram no longer terminates it; every unmarshal function checks the message type and the length field; the client accepts only a reply whose message type and request id match, and discards late or duplicate replies.
- **Callbacks**: sent from the server's main socket instead of a separate socket with a random port, which a firewall or NAT on the client side may block; a client that registers again for the same flight renews its registration instead of receiving duplicate callbacks.
- **Server output**: every request is printed with its source, arguments and the reply produced, as the project requires; duplicates are marked "executing AGAIN" or "NOT re-executed".
- **Client interface**: the monitor confirms the registration before waiting and waits for the remaining time only; place names may contain spaces; non-numeric input is asked again instead of ending the client.
- **Windows support**: [include/common/platform.h](include/common/platform.h) maps the socket calls to Winsock; Windows reports of ICMP "port unreachable" on UDP sockets are switched off; the build links ws2_32.
- **Build**: the Makefile tracks header dependencies; CMake and the Makefile both use C++17. The unit-test targets and helper scripts of the original version were removed because their test sources were never committed.
- **Comments**: all source comments are in English.

## 3. Additions

- **Web client** ([web/](web/)): a second client written in Python, covering the optional requirement of implementing the client and the server in different languages. `bridge.py` reimplements the protocol, marshalling, retransmission and the three semantics with the Python standard library and serves a booking page to the local browser; `selftest.py` is its integration test.
- **Demonstration launcher**: `demo.bat` and `demo.command` start the server in each experiment configuration, the web client or the C++ client from a numbered menu.
- **Command-line options**: `--semantics`, `--timeout-ms`, `--retries`, `--seed`, `--log-file`, all validated with range checks ([include/common/cli_options.h](include/common/cli_options.h)).

## 4. Verification

- Built with the Makefile on Ubuntu (g++ 15) without warnings under `-Wall -Wextra`; Windows binaries built for the MinGW-w64 target.
- `tools/test_semantics.sh`: all checks pass on Windows and Linux with the results above.
- `web/selftest.py`: all checks pass, also with the server on a Linux host and the web client on a Windows host.
- Cross-host runs between a Windows host and a Linux host, and between a Windows laptop and a MacBook on a phone hotspot: queries, reservations, callbacks and the at-least-once duplicate executions behave as described.
- Manual tests of invalid input, malformed datagrams, an unreachable server, and request and reply loss.

## 5. Notes

- The command-line options are not compatible with the original version: the original `--at-least-once` option no longer exists and is rejected.
- The original documents at the repository root (`docs/`) describe the old, reversed semantics; this folder is authoritative.
