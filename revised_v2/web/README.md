# Web client (Python bridge + browser page)

An optional second client for the flight server, in a different language than
the server: `bridge.py` is a **Python UDP client** of the C++ server, and it
serves a booking-site style page to a browser on the same computer.

```
laptop A:  browser ──HTTP (localhost)──▶ bridge.py ══UDP (LAN)══▶ laptop C: C++ server
laptop B:  browser ──HTTP (localhost)──▶ bridge.py ══UDP (LAN)══╯
```

The bridge runs **on each client laptop** (not on the server): the browser only
talks to `localhost`, while the UDP traffic really crosses the network. Each
browser tab becomes its own UDP client with its own port and request-id counter,
so two tabs on one laptop behave like two separate clients.

The wire protocol, marshalling, timeouts, retransmission and the three
invocation semantics are implemented in `bridge.py` with the Python standard
library only (`struct`, `socket`, `http.server`). Nothing in the C++ code is
changed or needed by the bridge except the running server.

## Requirements

- Python 3.8 or newer (macOS: `python3` from the Xcode command-line tools;
  Windows: any python.org or Anaconda Python). No packages to install.
- The C++ server from this folder's parent (`make`, then `./server`).

## Run

```bash
# on the server laptop
cd revised_v2 && ./server --port=8080 --semantics=at-most-once

# on each client laptop
cd revised_v2/web
python3 bridge.py --server=<server IP> --port=8080 --open
# Windows: python bridge.py --server=<server IP> --port=8080 --open
```

`--open` opens http://localhost:8000/ in the default browser. Open a second tab
to get a second client. The header shows the UDP port of the tab's client; the
same port appears in the server's log for that client's requests.

Options (`python3 bridge.py --help`):

| Option | Meaning | Default |
|---|---|---|
| `--server`, `--port` | flight server address | 127.0.0.1, 8080 |
| `--http-port` | port of the local web page | 8000 |
| `--semantics` | `at-most-once`, `at-least-once` or `maybe` for new tabs (use the same as the server) | at-most-once |
| `--timeout-ms`, `--retries` | reply timeout and number of retransmissions | 2000, 5 |
| `--loss-rate`, `--seed` | simulated *request* loss and its seed | 0, time |
| `--bind` | address of the local web page; keep the default unless another device must open this bridge's page | 127.0.0.1 |

Semantics, timeout, retries and loss rate can also be changed per tab in the page's Settings card.

## Self-test

`selftest.py` drives the bridge through its HTTP API like two browser tabs
would and checks all six services, the two-client callback scenario, the
server's validation errors and (with `--server-exe`) the semantics experiment:

```bash
cd revised_v2/web
python3 selftest.py --server-exe ../server       # Windows: python selftest.py --server-exe ../server.exe
```

## The page

- **Search / details / book / cancel / seat availability** – services 1, 2, 3, 6, 5.
- **Watch seat availability** – service 4. The page is not blocked while
  watching; every callback appears under *Live seat updates*. Callbacks are
  plain UDP datagrams (no acknowledgement), so a *Check* button is kept for
  the authoritative seat count.
- **Wire log** – every datagram this client sent, received or dropped, as hex
  bytes with the 12-byte header highlighted, plus timeouts and retransmissions.
  This is what to show when explaining marshalling and the invocation semantics.
- A non-idempotent call (book / cancel) that gets no reply is reported as
  *outcome unknown*: the server may have executed it (this is exactly the
  at-least-once hazard).

## Demo script (three laptops)

1. Server laptop: `./server --port=8080 --semantics=at-most-once`.
2. Laptop A: start the bridge, open the page, **Watch** flight 1001 for 120 s.
3. Laptop B: start the bridge, open the page, **Book** 2 seats on 1001.
   Laptop A's *Live seat updates* shows "Flight 1001 now has 118 seats".
   The server log shows two different client ports, both with request #1.
4. Semantics experiment (both rounds set the server's `--semantics` explicitly):
   - restart the server with `--semantics=at-least-once --loss-rate=0.5 --seed=6103`,
     set the tab's semantics to at-least-once, book 1 seat ten times, then *Check*:
     more than 10 seats are gone and the wire log shows the retransmissions
     that the server executed again;
   - restart with `--semantics=at-most-once --loss-rate=0.5 --seed=6103`
     (seats are back to 120), set the tab to at-most-once, repeat: exactly 10
     seats are gone — duplicates were answered from the server's history.

Keep timeout × (retries + 1) ≤ 30 s: the server forgets a reply after 60 s.

## Notes for the report

- Data representation across languages: Python `struct` with `!i` / `!I` / `!f`
  produces the same big-endian layout as `htonl()` and the IEEE-754 bit copy on
  the C++ side; string lengths are UTF-8 *byte* counts.
- Windows Firewall asks once whether `python.exe` may receive on the network
  (needed for callbacks from another laptop): allow it for private networks.
- The HTTP side listens on 127.0.0.1 only; nothing outside the laptop can
  reach the page.
