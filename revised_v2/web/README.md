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

Semantics, timeout, retries and loss rate can also be changed per tab in the page's Lab console.

## Self-test

`selftest.py` drives the bridge through its HTTP API like two browser tabs
would and checks all six services, the two-client callback scenario, the
server's validation errors and (with `--server-exe`) the semantics experiment:

```bash
cd revised_v2/web
python3 selftest.py --server-exe ../server       # Windows: python selftest.py --server-exe ../server.exe
```

## The page

A booking-site layout on top of the same six services. English / 中文 can be
switched at any time (top right); switching only re-renders the page – it sends
no request and keeps the tab's UDP client, search, selection and watches.

- **Search** (service 1) – From / To accept English or Chinese city names
  (北京 is sent as `Beijing`), plus the number of passengers. For every flight
  found the page then asks for its details (service 2) once, one after another,
  and shows departure time, fare and seats on a card. If a detail request
  fails the card says so and offers *Retry*; nothing is filled in by the page.
- **Select → Confirm booking** (service 3) – *Select* only opens the booking
  panel (no request). *Confirm booking* sends one reservation; the button is
  disabled until the reply arrives. The panel and the card then show the
  seat count from the server's reply. Fares are shown as stored on the server
  (the data defines no currency).
- **Return seats** (service 6) – in the booking panel. The server has no orders
  or accounts; this gives seats back to the flight. *This tab's activity* is a
  list kept by the page only.
- **Watch availability** (service 4) – registers for callbacks for the duration
  chosen in *Seat alerts*. Each callback updates the alert, the card and shows
  a short notification; the countdown is local and sends nothing. Callbacks
  are not acknowledged and can be lost, so each alert has a *Refresh* button
  (service 5). Nothing is polled in the background.
- **Result unknown** – a booking or return that gets no reply after all
  retransmissions is reported as "the request may have been executed – do not
  resubmit"; the page never retries it on its own.
- **Lab console** (top bar, stays open until closed) – this tab's UDP port and
  server, the invocation settings (semantics, timeout, retries, request loss),
  the six **raw service forms** (each submit is one logical call; on a timeout
  the bridge may resend it as further datagrams with the same request id), and the
  **wire log** with every datagram as hex (12-byte header highlighted),
  retransmissions and simulated losses. Protocol names stay in English to
  match the source code.

## Demo script (three laptops)

1. Server laptop: `./server --port=8080 --semantics=at-most-once`.
2. Laptop A: start the bridge, open the page, search Beijing → Shanghai and
   press **Watch availability** on flight 1001 (choose 2 min in *Seat alerts* first).
3. Laptop B: start the bridge, open the page, search the same route,
   **Select** flight 1001, set 2 passengers, **Confirm booking**.
   Laptop A's *Seat alerts* shows "Server update: 118 seats" and a notification.
   The server log shows two different client ports, both starting at request #1.
4. Semantics experiment – the console client (`../client`) is the reference
   tool for this. On the page, use the **raw forms in the Lab console**: every
   submit is one logical call (plus retransmissions with the same request id),
   whereas a search also makes one detail call per flight found, which shifts
   the sequence of simulated losses. Repeatable numbers additionally need a
   freshly started server (same seed), the same timeout and retries, and no
   other client sending in between. Set the server's `--semantics` explicitly
   in both rounds:
   - restart the server with `--semantics=at-least-once --loss-rate=0.5 --seed=6103`,
     set the tab's semantics to at-least-once, send *3 · Reserve seats* (1001, 1)
     ten times, then *5 · Seat count*: more than 10 seats are gone and the wire
     log shows the retransmissions the server executed again;
   - restart with `--semantics=at-most-once --loss-rate=0.5 --seed=6103`
     (seats are back to 120), reload the page (new client), set at-most-once,
     repeat: exactly 10 seats are gone – duplicates were answered from the
     server's history.

Keep timeout × (retries + 1) ≤ 30 s: the server forgets a reply after 60 s.

## Notes for the report

- Data representation across languages: Python `struct` with `!i` / `!I` / `!f`
  produces the same big-endian layout as `htonl()` and the IEEE-754 bit copy on
  the C++ side; string lengths are UTF-8 *byte* counts.
- Windows Firewall asks once whether `python.exe` may receive on the network
  (needed for callbacks from another laptop): allow it for private networks.
- The HTTP side listens on 127.0.0.1 only; nothing outside the laptop can
  reach the page.
