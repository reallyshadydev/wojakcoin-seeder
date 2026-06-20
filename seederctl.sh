#!/usr/bin/env bash
# Control the single multi-chain seeder process (all chains from seeder.conf at once).
#
# Usage: ./seederctl.sh {start|stop|restart|status|run}
#        ./seederctl.sh add <name> --src <coin-src> [--cli "..."]
#        ./seederctl.sh export <chain> [extra args for the export tool]
#
#   start    launch ONE dnsseed process serving every chain in seeder.conf (background)
#   stop     stop it
#   restart  stop then start
#   status   per-chain good/tracked node counts
#   run      run in the foreground (Ctrl-C) — good for testing / systemd
#   add      scaffold a new chain into seeder.conf from a coin's source tree
#   export   turn a chain's crawler dump into chainparams IP seeds
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$DIR/dnsseed"
CONF="$DIR/seeder.conf"
DATA="$DIR/data"
PIDFILE="$DATA/dnsseed.pid"
LOG="$DATA/dnsseed.log"
EXPORT_TOOL="$DIR/tools/export-chainparams-seeds.py"

is_running() { [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; }
need_bin()   { [ -x "$BIN" ] || { echo "Binary not built. Run 'make' first."; exit 1; }; }

# List the [chain] section names in seeder.conf.
chains() { grep -oE '^\s*\[[^]]+\]' "$CONF" | tr -d '[] \t'; }

# Read a key's value from a given [chain] section of seeder.conf.
conf_get() { # conf_get <chain> <key>
  awk -F= -v sec="[$1]" -v key="$2" '
    { line=$0; gsub(/^[ \t]+|[ \t]+$/,"",line) }
    line==sec { inblk=1; next }
    line ~ /^\[/ { inblk=0 }
    inblk { k=$1; gsub(/[ \t]/,"",k); if (k==key) { v=$2; gsub(/^[ \t]+|[ \t]+$/,"",v); print v; exit } }
  ' "$CONF"
}

case "${1:-}" in
  run)
    need_bin; mkdir -p "$DATA"
    echo "chains: $(chains | tr '\n' ' ')"
    cd "$DIR"; exec "$BIN" -c "$CONF" --datadir "$DATA"
    ;;
  start)
    need_bin
    if is_running; then echo "already running (pid $(cat "$PIDFILE"))"; exit 0; fi
    mkdir -p "$DATA"; cd "$DIR"
    nohup "$BIN" -c "$CONF" --datadir "$DATA" >> "$LOG" 2>&1 &
    echo $! > "$PIDFILE"
    echo "started (pid $(cat "$PIDFILE")) — chains: $(chains | tr '\n' ' ')"
    echo "log: $LOG"
    ;;
  stop)
    if is_running; then kill "$(cat "$PIDFILE")" && echo "stopped (pid $(cat "$PIDFILE"))"; else echo "not running"; fi
    rm -f "$PIDFILE"
    ;;
  restart)
    "$0" stop || true; sleep 1; "$0" start
    ;;
  status)
    if is_running; then echo "state: running (pid $(cat "$PIDFILE"))"; else echo "state: stopped"; fi
    for c in $(chains); do
      dump="$DATA/$c/dnsseed.dump"
      if [ -f "$dump" ]; then
        good=$(awk 'NR>1 && $2==1{g++} END{print g+0}' "$dump")
        tot=$(awk 'NR>1{t++} END{print t+0}' "$dump")
        printf "  %-12s %s good / %s tracked\n" "$c" "$good" "$tot"
      else
        printf "  %-12s no dump yet (written ~100s after start)\n" "$c"
      fi
    done
    ;;
  add)
    # Scaffold a new chain into seeder.conf from a coin's source tree (and optional daemon).
    # e.g. ./seederctl.sh add dogecoin --src /root/dogecoin-1.14.9
    shift
    [ -n "${1:-}" ] || { echo "usage: $0 add <name> --src <coin-src> [--cli \"...\" --minheight N ...]"; exit 2; }
    python3 "$DIR/tools/add-chain.py" "$@" --conf "$CONF"
    ;;
  export)
    chain="${2:-}"; [ -n "$chain" ] || { echo "usage: $0 export <chain>"; exit 2; }
    dump="$DATA/$chain/dnsseed.dump"
    [ -f "$dump" ] || { echo "no dump at $dump — is '$chain' a configured chain that has been running?"; exit 1; }
    port="$(conf_get "$chain" port)"; [ -n "$port" ] || { echo "no port for chain '$chain' in $CONF"; exit 1; }
    python3 "$EXPORT_TOOL" "$dump" --port "$port" --outdir "$DATA/$chain/export" "${@:3}"
    ;;
  *)
    echo "usage: $0 {start|stop|restart|status|run}"
    echo "       $0 add <name> --src <coin-src> [--cli \"...\"]"
    echo "       $0 export <chain> [extra args]"
    echo "chains in $CONF: $(chains | tr '\n' ' ')"
    exit 2
    ;;
esac
