#!/usr/bin/env bash
# Control the multi-chain DNS seeder on this server.
#
# Usage: ./seederctl.sh <chain> {start|stop|restart|status|run|export}
#        ./seederctl.sh list
#
#   <chain>   name of a preset in chains/<chain>.conf (e.g. wojakcoin, pepecoin)
#   start     launch the seeder for <chain> in the background (nohup + pidfile)
#   stop      stop the background seeder for <chain>
#   restart   stop then start
#   status    show pid + good/tracked node counts for <chain>
#   run       run in the foreground (Ctrl-C to quit) — good for testing / systemd
#   export    turn this chain's crawler dump into chainparams IP seeds
#
# Each chain gets its own state dir: data/<chain>/ (dnsseed.dat/dump/log/pid).
# Run several chains at once by starting each: ./seederctl.sh pepecoin start, etc.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$DIR/dnsseed"
EXPORT_TOOL="$DIR/tools/export-chainparams-seeds.py"

usage() { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

if [ "${1:-}" = "list" ]; then
  echo "Available chains:"; ls "$DIR/chains"/*.conf 2>/dev/null | sed 's#.*/##; s#\.conf$##; s/^/  /' || echo "  (none)"
  exit 0
fi

CHAIN="${1:-}"; CMD="${2:-}"
[ -z "$CHAIN" ] || [ -z "$CMD" ] && usage
CONF="$DIR/chains/$CHAIN.conf"
[ -f "$CONF" ] || { echo "No such chain preset: $CONF"; echo "Try: $0 list"; exit 1; }

DATA="$DIR/data/$CHAIN"
PIDFILE="$DATA/dnsseed.pid"
LOG="$DATA/dnsseed.log"
DUMP="$DATA/dnsseed.dump"

load_conf() {
  # defaults, overridden by the chain preset
  CHAIN=""; MAGIC=""; P2PORT=""; MINHEIGHT=""; MINVERSION=""; SEEDS=""
  HOST=""; NS=""; MBOX=""; DNSPORT="53"; LISTEN="::"; THREADS="64"
  # shellcheck disable=SC1090
  source "$CONF"
  [ -n "$MAGIC" ] && [ -n "$P2PORT" ] || { echo "$CONF must set MAGIC and P2PORT"; exit 1; }
}

build_args() {
  load_conf
  ARGS=( -t "$THREADS" -a "$LISTEN" -p "$DNSPORT" --magic "$MAGIC" --p2port "$P2PORT" )
  [ -n "$MINHEIGHT" ]  && ARGS+=( --minheight "$MINHEIGHT" )
  [ -n "$MINVERSION" ] && ARGS+=( --minversion "$MINVERSION" )
  for s in $SEEDS; do ARGS+=( -s "$s" ); done
  if [ -n "$HOST" ] && [ -n "$NS" ]; then
    ARGS+=( -h "$HOST" -n "$NS" )
    [ -n "$MBOX" ] && ARGS+=( -m "$MBOX" )
    MODE="DNS seed for '$HOST' on port $DNSPORT (magic $MAGIC, p2p $P2PORT)"
  else
    MODE="crawler-only (magic $MAGIC, p2p $P2PORT; set HOST/NS in $CONF for DNS)"
  fi
}

is_running() { [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; }
need_bin()  { [ -x "$BIN" ] || { echo "Binary not built. Run 'make' first."; exit 1; }; }

case "$CMD" in
  run)
    need_bin; mkdir -p "$DATA"; build_args
    echo "[$CHAIN] mode: $MODE"
    echo "[$CHAIN] exec: dnsseed ${ARGS[*]}"
    cd "$DATA"; exec "$BIN" "${ARGS[@]}"
    ;;
  start)
    need_bin
    if is_running; then echo "[$CHAIN] already running (pid $(cat "$PIDFILE"))"; exit 0; fi
    mkdir -p "$DATA"; build_args
    cd "$DATA"
    nohup "$BIN" "${ARGS[@]}" >> "$LOG" 2>&1 &
    echo $! > "$PIDFILE"
    echo "[$CHAIN] started (pid $(cat "$PIDFILE")) — $MODE"
    echo "[$CHAIN] log: $LOG"
    ;;
  stop)
    if is_running; then kill "$(cat "$PIDFILE")" && echo "[$CHAIN] stopped (pid $(cat "$PIDFILE"))"; else echo "[$CHAIN] not running"; fi
    rm -f "$PIDFILE"
    ;;
  restart)
    "$0" "$CHAIN" stop || true; sleep 1; "$0" "$CHAIN" start
    ;;
  status)
    if is_running; then echo "[$CHAIN] state: running (pid $(cat "$PIDFILE"))"; else echo "[$CHAIN] state: stopped"; fi
    if [ -f "$DUMP" ]; then
      good=$(awk 'NR>1 && $2==1{c++} END{print c+0}' "$DUMP")
      tot=$(awk 'NR>1{c++} END{print c+0}' "$DUMP")
      echo "[$CHAIN] nodes: $good good / $tot tracked  ($DUMP)"
    else
      echo "[$CHAIN] nodes: no dnsseed.dump yet (written ~100s after start)"
    fi
    ;;
  export)
    [ -f "$DUMP" ] || { echo "[$CHAIN] no dump at $DUMP — start the crawler first"; exit 1; }
    load_conf
    out="$DATA/export"
    python3 "$EXPORT_TOOL" "$DUMP" --port "$P2PORT" --outdir "$out" "${@:3}"
    ;;
  *) usage ;;
esac
