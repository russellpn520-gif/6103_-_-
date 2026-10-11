#!/bin/bash
# SC6103 demo launcher for macOS / Linux: double-click in Finder (or run ./demo.command),
# then choose a number. Open one window per program (server, web client, C++ client).
cd "$(dirname "$0")" || exit 1

ask_ip() {
    IP=127.0.0.1
    [ -f server_ip.txt ] && IP=$(head -n 1 server_ip.txt | tr -d '[:space:]')
    echo
    echo "Server IP: 127.0.0.1 if the server runs on this computer,"
    echo "           otherwise the IP of computer 1 (ipconfig getifaddr en0 on a Mac)."
    read -r -p "Server IP [$IP]: " NEWIP
    [ -n "$NEWIP" ] && IP=$NEWIP
    echo "$IP" > server_ip.txt
}

need_binary() {
    if [ ! -x "./$1" ]; then
        echo "./$1 is missing; building it with make ..."
        make || { echo "make failed (macOS: run xcode-select --install first)"; return 1; }
    fi
}

run() {
    echo
    echo "> $*"
    echo "  Stop with Ctrl+C, then press Enter to come back to the menu."
    echo
    "$@"
    echo
    read -r -p "Press Enter to return to the menu " _ || exit 0
}

trap 'echo' INT   # Ctrl+C stops the program, not this menu

clear   # only once, so earlier server output stays readable above the menu
while true; do
    echo
    cat <<'EOF'
================= SC6103 demo launcher =================
  Server  (computer 1)
    1  Server - main flow          at-most-once, no loss
    2  Server - semantics round 1  at-least-once, reply loss 0.5, seed 6103
    3  Server - semantics round 2  at-most-once,  reply loss 0.5, seed 6103
    4  Server - request-loss demo  at-least-once, no reply loss

  Clients (computers 2 and 3)
    5  Web client  (bridge + browser)
    6  C++ client  at-most-once     main flow / round 2
    7  C++ client  at-least-once    round 1
    8  C++ client  request loss 0.5, seed 7   (with server 4)

    0  Exit
========================================================
EOF
    read -r -p "Choose: " CHOICE || exit 0   # input closed
    case "$CHOICE" in
        1) need_binary server && run ./server --port=8080 --semantics=at-most-once --loss-rate=0 --log-file=demo_server.log ;;
        2) need_binary server && run ./server --port=8080 --semantics=at-least-once --loss-rate=0.5 --seed=6103 --log-file=demo_server.log ;;
        3) need_binary server && run ./server --port=8080 --semantics=at-most-once --loss-rate=0.5 --seed=6103 --log-file=demo_server.log ;;
        4) need_binary server && run ./server --port=8080 --semantics=at-least-once --loss-rate=0 --log-file=demo_server.log ;;
        5) ask_ip
           PY=$(command -v python3 || command -v python)
           if [ -z "$PY" ]; then echo "Python 3 not found"; read -r -p "Press Enter " _; continue; fi
           run "$PY" web/bridge.py --server="$IP" --port=8080 --open ;;
        6) need_binary client && ask_ip && run ./client --server="$IP" --port=8080 --semantics=at-most-once ;;
        7) need_binary client && ask_ip && run ./client --server="$IP" --port=8080 --semantics=at-least-once ;;
        8) need_binary client && ask_ip && run ./client --server="$IP" --port=8080 --semantics=at-least-once --loss-rate=0.5 --seed=7 ;;
        0) exit 0 ;;
    esac
done
