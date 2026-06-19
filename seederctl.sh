#!/usr/bin/env bash
# Control the single WojakCoin DNS seeder on this server.
#
# Usage: ./seederctl.sh {start|stop|restart|status|run}
#   start    - launch in the background (nohup + pidfile)
#   stop     - stop the background seeder
#   restart  - stop then start
#   status   - show pid + good/tracked node counts
#   run      - run in the foreground (Ctrl-C to quit); good for testing / systemd
#
# Config is read from ./seeder.conf. Runtime state (dnsseed.dat/dump/log) lives in ./data.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$DIR/dnsseed"
CONF="$DIR/seeder.conf"
DATA="$DIR/data"
PIDFILE="$DATA/dnsseed.pid"
LOG="$DATA/dnsseed.log"

build_args() {
  # defaults, overridden by seeder.conf
  HOST=""; NS=""; MBOX=""; DNSPORT="53"; LISTEN="::"; THREADS="64"; MINHEIGHT="130000"; EXTRA_SEEDS=""
  # shellcheck disable=SC1090
  [ -f "$CONF" ] && source "$CONF"
  ARGS=( -t "$THREADS" -a "$LISTEN" -p "$DNSPORT" --minheight "$MINHEIGHT" )
  if [ -n "$HOST" ] && [ -n "$NS" ]; then
    ARGS+=( -h "$HOST" -n "$NS" )
    [ -n "$MBOX" ] && ARGS+=( -m "$MBOX" )
    MODE="DNS seed for '$HOST' on port $DNSPORT"
  else
    MODE="crawler-only (set HOST/NS in seeder.conf to serve DNS)"
  fi
  for s in $EXTRA_SEEDS; do ARGS+=( -s "$s" ); done
}

is_running() { [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; }

need_bin() { [ -x "$BIN" ] || { echo "Binary not built. Run 'make' first."; exit 1; }; }

case "${1:-}" in
  run)
    need_bin; mkdir -p "$DATA"; build_args
    echo "mode: $MODE"
    echo "exec: dnsseed ${ARGS[*]}"
    cd "$DATA"
    exec "$BIN" "${ARGS[@]}"
    ;;
  start)
    need_bin
    if is_running; then echo "already running (pid $(cat "$PIDFILE"))"; exit 0; fi
    mkdir -p "$DATA"; build_args
    cd "$DATA"
    nohup "$BIN" "${ARGS[@]}" >> "$LOG" 2>&1 &
    echo $! > "$PIDFILE"
    echo "started (pid $(cat "$PIDFILE")) — mode: $MODE"
    echo "log: $LOG"
    ;;
  stop)
    if is_running; then kill "$(cat "$PIDFILE")" && echo "stopped (pid $(cat "$PIDFILE"))"; else echo "not running"; fi
    rm -f "$PIDFILE"
    ;;
  restart)
    "$0" stop || true
    sleep 1
    "$0" start
    ;;
  status)
    if is_running; then echo "state: running (pid $(cat "$PIDFILE"))"; else echo "state: stopped"; fi
    if [ -f "$DATA/dnsseed.dump" ]; then
      good=$(awk 'NR>1 && $2==1 {c++} END{print c+0}' "$DATA/dnsseed.dump")
      tot=$(awk 'NR>1 {c++} END{print c+0}' "$DATA/dnsseed.dump")
      echo "nodes: $good good / $tot tracked  ($DATA/dnsseed.dump)"
    else
      echo "nodes: no dnsseed.dump yet (written ~100s after start)"
    fi
    ;;
  *)
    echo "usage: $0 {start|stop|restart|status|run}"; exit 2
    ;;
esac
