# Distributed Flight Information System (revised_v2)

SC6103 Distributed Systems course project: a distributed flight information system in which C++ clients and a C++ server communicate over UDP. It offers flight queries, seat reservations, server callbacks for seat monitoring, and both at-least-once and at-most-once invocation semantics, with experiments that compare them. A second client written in Python (`web/`) talks to the same server.

> This folder is a self-contained revision of the code at the repository root, which is left unchanged. [CHANGES.md](CHANGES.md) lists what was changed and why.
> All commands below are run inside this folder: `cd revised_v2`.

## Contents

- [Features](#features)
- [Requirements and build](#requirements-and-build)
- [Quick start](#quick-start)
- [Demo launcher](#demo-launcher)
- [Command-line options](#command-line-options)
- [Invocation semantics](#invocation-semantics)
- [Message format](#message-format)
- [Tests and experiments](#tests-and-experiments)
- [Running on several computers](#running-on-several-computers)
- [Web client](#web-client)
- [Troubleshooting](#troubleshooting)
- [Project structure](#project-structure)

## Features

### Services

| # | Service | Idempotent | Description |
|---|---|---|---|
| 1 | Query flights by source and destination | yes | Returns every matching flight id; an error if none matches |
| 2 | Get flight information | yes | Departure time, airfare and seat availability; an error if the flight does not exist |
| 3 | Reserve seats | **no** | Every execution removes seats; distinct errors for an unknown flight, a non-positive seat count and insufficient seats |
| 4 | Monitor seat availability | — | During the monitor interval the server sends the new seat count to every registered client after each reservation or cancellation (callback) |
| 5 | Get seat count (additional) | yes | Read-only, gives the same result however often it is executed |
| 6 | Cancel reservation (additional) | **no** | Every execution gives seats back; it cannot return more seats than are reserved |

### Distributed-system features

- **Own message format and marshalling**: integers in network byte order (`htonl`/`ntohl`), length-prefixed UTF-8 strings, floats as IEEE-754 bit patterns. No RMI, RPC or serialisation library is used.
- **Invocation semantics** chosen on the command line: at-least-once and at-most-once, plus maybe on the client as a baseline.
- **Duplicate filtering** by (client IP, client port, request id), with a reply history kept for 60 seconds.
- **Loss simulation**: the client can drop requests and the server can drop replies, with a seed so that every experiment can be repeated.
- **Callbacks** are sent from the server's main socket; registering again for the same flight renews the registration instead of producing duplicate callbacks.
- **Portable**: Linux, macOS and Windows (Winsock).

## Requirements and build

| Platform | Compiler | Build |
|---|---|---|
| Linux | g++ with C++17 | `make` |
| macOS | Xcode Command Line Tools (clang), `xcode-select --install` | `make` |
| Windows | MinGW-w64 g++, for example MSYS2 `mingw-w64-ucrt-x86_64-gcc` | `make` in the MSYS2 UCRT64 shell, or `mingw32-make` |

```bash
make            # builds ./server and ./client (server.exe and client.exe on Windows)
make clean      # removes the build output
```

`CMakeLists.txt` builds the same two programs with CMake. The web client needs Python 3.8 or newer and no extra packages.

## Quick start

```bash
./server --port=8080 --semantics=at-most-once
./client --server=127.0.0.1 --port=8080 --semantics=at-most-once
```

```
========== Flight Information System ==========
1. Query flights by source and destination
2. Get flight information
3. Reserve seats
4. Get seat count
5. Cancel reservation
6. Monitor seat availability
0. Exit
===============================================
Enter your choice (0-6):
```

Choose `1`, then `Beijing` and `Shanghai`: the server returns flights 1001, 1002 and 1003. Enter `0` to leave the client and press `Ctrl+C` to stop the server.

## Demo launcher

`demo.bat` (Windows, double-click) and `demo.command` (macOS/Linux, double-click or `./demo.command`) start every configuration from a numbered menu:

| Number | Starts |
|---|---|
| 1 | Server, main flow: at-most-once, no simulated loss |
| 2 | Server, semantics round 1: at-least-once, reply loss 0.5, seed 6103 |
| 3 | Server, semantics round 2: at-most-once, reply loss 0.5, seed 6103 |
| 4 | Server, request-loss demo: at-least-once, no reply loss |
| 5 | Web client (Python bridge and browser page) |
| 6 | C++ client, at-most-once |
| 7 | C++ client, at-least-once |
| 8 | C++ client with request loss 0.5, seed 7 |

The client options ask for the server's IP address and remember it in `server_ip.txt`. The server options also append their log to `demo_server.log`. Stop a program with `Ctrl+C` to return to the menu (on Windows answer `N` to "Terminate batch job" and press a key).

## Command-line options

### Server

| Option | Meaning | Default |
|---|---|---|
| `--port=<port>` | UDP port to listen on | 8080 |
| `--semantics=<s>` | `at-most-once` (filter duplicates) or `at-least-once` (execute every request) | at-most-once |
| `--loss-rate=<p>` | probability 0.0-1.0 that a **reply** is dropped | 0 |
| `--seed=<n>` | seed of the loss simulation; the same seed repeats an experiment | current time |
| `--log-file=<file>` | also append the log to a file | console only |

### Client

| Option | Meaning | Default |
|---|---|---|
| `--server=<ip>` | server IPv4 address | 127.0.0.1 |
| `--port=<port>` | server port | 8080 |
| `--semantics=<s>` | `at-most-once` or `at-least-once` (both retransmit after a timeout), or `maybe` (no retransmission) | at-most-once |
| `--timeout-ms=<ms>` | time to wait for a reply before retransmitting | 2000 |
| `--retries=<n>` | retransmissions after the first attempt | 5 |
| `--loss-rate=<p>` | probability 0.0-1.0 that a **request** is dropped | 0 |
| `--seed=<n>` | seed of the loss simulation | current time |
| `--log-file=<file>` | also append the log to a file | console only |

Both programs reject invalid options and out-of-range values. The server prints every request it receives (source address, request id, arguments) and every reply it produces (status and content), and marks duplicate requests and simulated losses.

## Invocation semantics

As defined in the lecture on remote invocation:

| Semantics | Client retransmits after a timeout | Server filters duplicates | A duplicate request is | Correct for |
|---|---|---|---|---|
| maybe | no | — | — | baseline only |
| at-least-once | yes | no | **executed again** | idempotent operations only |
| at-most-once | yes | yes | answered with the stored reply, **not executed again** | all operations |

- The client behaves the same under at-least-once and at-most-once: it retransmits the same bytes with the **same request id**. The difference is made by the server, so start both ends with the same `--semantics`.
- Request ids are unique only within one client (every client starts at 1), so the server identifies a request by (client IP, client port, request id).
- Replies stay in the history for 60 seconds, longer than the client's whole retransmission window (2 s x 6 attempts by default).

```bash
# at-least-once with lost replies: a non-idempotent operation can be executed twice
./server --semantics=at-least-once --loss-rate=0.5 --seed=6103
./client --semantics=at-least-once
```

## Message format

Every integer is 32 bits in network byte order. Every message starts with a 12-byte header:

```
+--------------+--------------+------------------+---------------------------+
| message_type |  request_id  |  message_length  |  body (depends on type)   |
|    uint32    |    uint32    | uint32, total    |                           |
+--------------+--------------+------------------+---------------------------+
```

Strings are a uint32 byte length followed by the UTF-8 bytes; the departure time is five int32 values (year, month, day, hour, minute); the airfare is the IEEE-754 bit pattern of a float. Callbacks carry request id 0. The layout of all 13 message types is documented in [include/common/message_serializer.h](include/common/message_serializer.h). Unmarshalling checks the message type and the length field; malformed datagrams are dropped and logged.

## Tests and experiments

### Functional test (manual)

Start `./server`, then `./client`, and enter:

| Service | Input | Expected |
|---|---|---|
| 1 | `1` → `Beijing` → `Shanghai` | 3 flights |
| 2 | `2` → `1001` | details of flight 1001, 120 seats |
| 3 | `3` → `1001` → `2` | reservation successful, 118 left |
| 3 (error) | `3` → `9999` → `1` | Flight not found |
| 5 | `4` → `1001` | 118 |
| 6 | `5` → `1001` → `1` | cancellation successful, 119 |
| 4 | second client: `6` → `1001` → `60`, then reserve in the first client | the monitoring client prints `[Callback] ...` |

### Invocation-semantics experiment (automated)

```bash
make
./tools/test_semantics.sh
```

The server drops each reply with probability 0.5 and the client retransmits after a timeout. The script runs three experiments:

1. **Non-idempotent operation**: 20 one-seat reservations under each semantics, comparing the seats actually taken.
2. **Idempotent operation**: 20 seat-count queries under at-least-once, which stay correct.
3. **Demonstration scenario**: one client monitors while another reserves; both start with request #1.

Sample output (seed 6103):

```
  semantics      successes  errors  retransmissions  executions  seats taken
  maybe                  9      11                0          20           20
  at-least-once         20       0               15          35           35
  at-most-once          20       0               15          20           20
```

Parameters can be changed with environment variables, for example `N=50 LOSS=0.3 SEED=1 ./tools/test_semantics.sh`. The server and client logs of every run are kept in `experiment_logs/`.

### Web client test

```bash
python3 web/selftest.py --server-exe ./server
```

## Running on several computers

The demonstration runs the server and two clients on three computers: one client monitors a flight, the other reserves seats.

1. Find the server computer's IP address: `ipconfig` on Windows, `ipconfig getifaddr en0` on macOS, `ip addr` on Linux.
2. Server: `./server --port=8080 --semantics=at-most-once`
3. Monitoring client: `./client --server=<server IP> --port=8080`, choose `6`, enter the flight id and the interval.
4. Booking client: `./client --server=<server IP> --port=8080` (or the web client), reserve seats on the same flight: the monitoring client prints `[Callback] ...`.

**Network and firewall.** Put all computers on the same network; a phone hotspot avoids campus Wi-Fi that blocks traffic between devices. On Windows, set the network to "Private", or allow `server.exe`, `client.exe` and `python.exe` for both private and public networks when the firewall asks. Otherwise the server does not receive requests, or a monitoring client does not receive callbacks.

## Web client

`web/` contains a second client written in Python, which covers the optional requirement of implementing the client and the server in different languages. `bridge.py` implements the message format, marshalling, timeouts, retransmission and the three semantics with the Python standard library, acts as a UDP client of this C++ server, and serves a booking page to the browser on the same computer. Each browser tab is its own UDP client, and the page's lab console shows every datagram in hexadecimal. See [web/README.md](web/README.md).

```bash
python3 web/bridge.py --server=<server IP> --port=8080 --open
```

## Troubleshooting

**The client keeps reporting "No reply from server".** Check that the server is running, that `--server` and `--port` are correct, and that the server computer's firewall allows the server to receive UDP on that network.

**The seat count is wrong under at-least-once.** That is what the experiment demonstrates: a reply was lost, the client retransmitted, and the server executed the same reservation again. Use at-most-once.

**A simulated loss rate shows no effect.** Losses are random. Use a higher rate such as 0.5, more operations, or a fixed `--seed`.

**`make: command not found`.** macOS: `xcode-select --install`; Ubuntu: `sudo apt-get install build-essential`; Windows: install MSYS2 and run `pacman -S mingw-w64-ucrt-x86_64-gcc make`.

**The server reports "Failed to bind".** Another server already uses the port: stop it or choose another `--port` for the server and all clients.

## Project structure

```
revised_v2/
├── include/
│   ├── common/            message types, marshalling, UDP socket, platform layer, logger, options
│   ├── server/            FlightServer, FlightManager, MonitorManager, RequestHistory
│   └── client/            FlightClient (requests, retransmission), ClientUI (menu)
├── src/                   implementations of the headers above, server_main.cpp, client_main.cpp
├── web/                   Python bridge, browser page, integration test
├── tools/
│   └── test_semantics.sh  automated invocation-semantics experiment
├── demo.bat, demo.command demonstration launcher
├── Makefile, CMakeLists.txt
├── CHANGES.md             changes compared with the original version at the repository root
└── README.md
```
