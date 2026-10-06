#!/usr/bin/env bash
#
# Invocation-semantics experiment (project description, section 3.3).
#
# Reply loss is simulated at the server (--loss-rate), so the client times out
# and retransmits requests that the server has in fact already executed. The
# script compares what each invocation semantics does with these duplicates:
#
#   Experiment 1  Non-idempotent operation: N reservations of 1 seat each.
#                   maybe          no retransmission: the client reports failures
#                                  although the server did reserve the seats
#                   at-least-once  every retransmission is executed again, so
#                                  MORE than N seats are taken  -> wrong result
#                   at-most-once   duplicates are answered from the reply history,
#                                  so exactly N seats are taken -> correct result
#   Experiment 2  Idempotent operation: N seat-count queries under at-least-once.
#                 Executing a query again does no harm: every answer is correct.
#   Experiment 3  The demo scenario under at-most-once: one client monitors the
#                 flight while another one reserves seats. Both clients number
#                 their first request #1; duplicates are detected per (client
#                 address, request id), so the reservation is executed (not
#                 mistaken for a duplicate) and the monitor gets the callback.
#
# Usage: build first (make), then run  ./tools/test_semantics.sh
# Settings can be changed through environment variables, for example
#        N=50 LOSS=0.3 ./tools/test_semantics.sh
# The server and client logs of every run are kept in experiment_logs/.

set -u
cd "$(dirname "$0")/.." || exit 1

N=${N:-20}                    # operations per run
LOSS=${LOSS:-0.5}             # probability that the server drops a reply
SEED=${SEED:-6103}            # seed of the loss simulation (same seed -> same losses)
TIMEOUT_MS=${TIMEOUT_MS:-300} # client retransmission timeout
RETRIES=${RETRIES:-8}         # client retransmissions per request
PORT=${PORT:-9300}            # each run uses the next port
FLIGHT=1001                   # test flight ...
INITIAL_SEATS=120             # ... and its seats in the test data
LOG_DIR=experiment_logs

if [ -f ./server.exe ]; then EXE=.exe; else EXE=; fi
SERVER=./server$EXE
CLIENT=./client$EXE
if [ ! -f "$SERVER" ] || [ ! -f "$CLIENT" ]; then
    echo "server/client not found - build them first with: make"
    exit 1
fi
mkdir -p "$LOG_DIR"

PASS=0
FAIL=0
SERVER_PID=""

pass() { echo "  [PASS] $1"; PASS=$((PASS + 1)); }
fail() { echo "  [FAIL] $1"; FAIL=$((FAIL + 1)); }

# start_server <semantics> <reply loss rate> <log name>
start_server() {
    PORT=$((PORT + 1))
    "$SERVER" --port=$PORT --semantics="$1" --loss-rate="$2" --seed=$SEED > "$LOG_DIR/$3-server.log" 2>&1 &
    SERVER_PID=$!
    sleep 1
}

stop_server() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
        SERVER_PID=""
    fi
}
trap stop_server EXIT

# run_client <semantics> <log name>; the menu input is read from stdin
run_client() {
    "$CLIENT" --port=$PORT --semantics="$1" --timeout-ms=$TIMEOUT_MS --retries=$RETRIES --seed=$SEED \
        > "$LOG_DIR/$2-client.log" 2>&1
}

# count <pattern> <file>: number of matching lines
count() {
    tr -d '\r' < "$2" 2>/dev/null | grep -c -- "$1" || true   # tr: Windows builds write CRLF
}

# seat_count <log name>: current seats of the test flight, asked by a new
# client that retransmits (a query is idempotent, so this is safe in any mode)
seat_count() {
    printf '4\n%s\n0\n' "$FLIGHT" | run_client at-least-once "$1"
    grep -o 'Available seats: -*[0-9]*' "$LOG_DIR/$1-client.log" | tail -n 1 | awk '{print $3}'
}

echo "=================================================================="
echo " Invocation semantics experiment"
echo " $N operations per run, server drops each reply with p = $LOSS (seed $SEED)"
echo " client timeout $TIMEOUT_MS ms, up to $RETRIES retransmissions"
echo "=================================================================="

# ------------------------------------------------------------------
echo ""
echo "Experiment 1: non-idempotent operation ($N x reserve 1 seat on flight $FLIGHT, $INITIAL_SEATS seats)"
echo ""
printf '  %-14s %9s %7s %16s %11s %12s\n' "semantics" "successes" "errors" "retransmissions" "executions" "seats taken"
printf '  %-14s %9s %7s %16s %11s %12s\n' "---------" "---------" "------" "---------------" "----------" "-----------"

RESULTS=""
for SEM in maybe at-least-once at-most-once; do
    # "maybe" differs from at-least-once only on the client (no retransmission)
    SERVER_SEM=$SEM
    [ "$SEM" = maybe ] && SERVER_SEM=at-least-once

    start_server "$SERVER_SEM" "$LOSS" "exp1-$SEM"
    { for _ in $(seq 1 "$N"); do printf '3\n%s\n1\n' "$FLIGHT"; done; printf '0\n'; } | run_client "$SEM" "exp1-$SEM"
    FINAL=$(seat_count "exp1-$SEM-final")
    stop_server

    CLOG="$LOG_DIR/exp1-$SEM-client.log"
    SLOG="$LOG_DIR/exp1-$SEM-server.log"
    OK=$(count 'Reservation successful' "$CLOG")
    ERRORS=$(count 'Error: ' "$CLOG")
    RETRANS=$(count 'retransmitting' "$CLOG")
    EXECUTED=$(count '] Request #[0-9]* from .*: RESERVE_SEATS' "$SLOG")
    TAKEN=$((INITIAL_SEATS - ${FINAL:-$INITIAL_SEATS}))

    printf '  %-14s %9s %7s %16s %11s %12s\n' "$SEM" "$OK" "$ERRORS" "$RETRANS" "$EXECUTED" "$TAKEN"
    RESULTS="$RESULTS $SEM:$OK:$ERRORS:$EXECUTED:$TAKEN"
done

echo ""
for RESULT in $RESULTS; do
    IFS=: read -r SEM OK ERRORS EXECUTED TAKEN <<< "$RESULT"
    case $SEM in
        maybe)
            if [ "$ERRORS" -gt 0 ] && [ "$TAKEN" -gt "$OK" ]; then
                pass "maybe: client was told $ERRORS reservation(s) failed, yet $TAKEN seats were taken for $OK successes"
            else
                fail "maybe: expected failed calls whose seats were nevertheless taken (no reply lost? raise LOSS or N)"
            fi ;;
        at-least-once)
            if [ "$TAKEN" -gt "$N" ]; then
                pass "at-least-once gives a WRONG result: $TAKEN seats taken for $N one-seat reservations ($EXECUTED executions)"
            else
                fail "at-least-once: no re-execution observed (no reply lost? raise LOSS or N)"
            fi ;;
        at-most-once)
            if [ "$EXECUTED" -le "$N" ] && [ "$TAKEN" -eq "$EXECUTED" ] && [ "$TAKEN" -ge "$OK" ]; then
                pass "at-most-once is correct: each reservation executed once, $TAKEN seats taken for $OK successful reservations"
            else
                fail "at-most-once: $EXECUTED executions and $TAKEN seats taken for $N requests"
            fi ;;
    esac
done

# ------------------------------------------------------------------
echo ""
echo "Experiment 2: idempotent operation under at-least-once ($N x get seat count of flight $FLIGHT)"
echo ""
start_server at-least-once "$LOSS" exp2
{ for _ in $(seq 1 "$N"); do printf '4\n%s\n' "$FLIGHT"; done; printf '0\n'; } | run_client at-least-once exp2
stop_server

ANSWERS=$(count 'Available seats:' "$LOG_DIR/exp2-client.log")
CORRECT=$(count "Available seats: $INITIAL_SEATS\$" "$LOG_DIR/exp2-client.log")
REEXEC=$(count 'executing AGAIN' "$LOG_DIR/exp2-server.log")
echo "  $ANSWERS answers, $CORRECT of them correct ($INITIAL_SEATS seats); $REEXEC duplicate request(s) were executed again"
if [ "$ANSWERS" -gt 0 ] && [ "$CORRECT" -eq "$ANSWERS" ] && [ "$REEXEC" -gt 0 ]; then
    pass "re-executing an idempotent operation does no harm: every answer is correct"
elif [ "$REEXEC" -eq 0 ]; then
    fail "no re-execution observed (no reply lost? raise LOSS or N)"
else
    fail "wrong answers for an idempotent query"
fi

# ------------------------------------------------------------------
echo ""
echo "Experiment 3: monitor + reservation from two clients, at-most-once, no loss"
echo "              (both clients number their first request #1)"
echo ""
start_server at-most-once 0 exp3
printf '6\n%s\n3\n0\n' "$FLIGHT" | run_client at-most-once exp3-monitor &
MONITOR_PID=$!
sleep 1
printf '3\n%s\n2\n0\n' "$FLIGHT" | run_client at-most-once exp3-reserve
wait "$MONITOR_PID"
stop_server

if grep -q "Reservation successful! Remaining seats: $((INITIAL_SEATS - 2))" "$LOG_DIR/exp3-reserve-client.log"; then
    pass "reservation #1 of the second client was executed, not mistaken for request #1 of the first client"
else
    fail "reservation of the second client did not succeed"
fi
if grep -q "\[Callback\] Flight $FLIGHT seat availability is now $((INITIAL_SEATS - 2))" "$LOG_DIR/exp3-monitor-client.log"; then
    pass "monitoring client received the callback with the new seat availability"
else
    fail "monitoring client did not receive the callback"
fi

# ------------------------------------------------------------------
echo ""
echo "=================================================================="
echo " Passed: $PASS   Failed: $FAIL   (logs: $LOG_DIR/)"
echo "=================================================================="
[ "$FAIL" -eq 0 ]
