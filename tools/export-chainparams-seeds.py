#!/usr/bin/env python3
"""
Turn crawler output (dnsseed.dump) into WojakCoin fixed-seed lists.

The DNS seeder writes a `dnsseed.dump` in each instance directory describing every
node it tracks. This tool reads one or more of those dumps, keeps the nodes that
are currently "good" (and optionally above an uptime threshold), de-duplicates
across instances, and emits:

  * nodes_main.txt          - plain "ip:port" list (one per line)
  * chainparamsseeds.snippet.h - a pnSeed6_main[] C array, ready to paste into
                              wojakcore/src/chainparamsseeds.h if you ever want to
                              refresh the IP seeds baked into the wallet.

This does NOT modify wojakcore. It only produces text you can review and use.

Examples:
  # merge the three instance dumps, keep good nodes, default port 20759
  ./export-chainparams-seeds.py instances/*/dnsseed.dump

  # only nodes with >=50% reliability over the 7-day window, cap at 30 seeds
  ./export-chainparams-seeds.py --min-uptime 50 --max 30 instances/seed1/dnsseed.dump
"""
import argparse
import ipaddress
import os
import sys

DEFAULT_PORT = 20759

# uptime columns in dnsseed.dump, in field order: 2h, 8h, 1d, 7d, 30d
UPTIME_FIELDS = {"2h": 3, "8h": 4, "1d": 5, "7d": 6, "30d": 7}


def parse_addr(token):
    """'1.2.3.4:20759' or '[2001:db8::1]:20759' -> (ipaddress, port)."""
    token = token.strip()
    if token.startswith("["):  # bracketed IPv6
        host, _, port = token[1:].partition("]")
        port = port.lstrip(":")
    else:
        host, _, port = token.rpartition(":")
        if not host:  # no colon at all
            host, port = token, ""
    ip = ipaddress.ip_address(host)
    return ip, int(port) if port else DEFAULT_PORT


def read_dump(path, want_port, min_uptime, window):
    """Yield (ip, port, uptime30d) for good nodes in a dnsseed.dump file."""
    col = UPTIME_FIELDS[window]
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.lstrip().startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 8:
                continue
            try:
                ip, port = parse_addr(parts[0])
                good = int(parts[1])
                uptime = float(parts[col].rstrip("%"))
                up30 = float(parts[UPTIME_FIELDS["30d"]].rstrip("%"))
            except (ValueError, IndexError):
                continue
            if good != 1:
                continue
            if port != want_port:
                continue
            if uptime < min_uptime:
                continue
            yield ip, port, up30


def to_seedspec6_bytes(ip):
    """Return 16 bytes for an ipaddress (IPv4 -> v6-mapped), matching SeedSpec6."""
    if ip.version == 4:
        return b"\x00" * 10 + b"\xff\xff" + ip.packed
    return ip.packed  # 16 bytes


def fmt_c_row(ip, port):
    data = ",".join("0x%02x" % b for b in to_seedspec6_bytes(ip))
    return "    {{%s}, %d},  // %s" % (data, port, ip)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dumps", nargs="+", help="one or more dnsseed.dump files")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT,
                    help="only keep nodes on this P2P port (default %(default)s)")
    ap.add_argument("--min-uptime", type=float, default=0.0,
                    help="minimum %% reliability in the selected window (default 0)")
    ap.add_argument("--window", choices=list(UPTIME_FIELDS), default="7d",
                    help="uptime window used for --min-uptime (default 7d)")
    ap.add_argument("--max", type=int, default=0,
                    help="cap the number of seeds emitted (0 = no cap)")
    ap.add_argument("--outdir", default=".",
                    help="directory to write nodes_main.txt / snippet (default .)")
    args = ap.parse_args()

    best = {}  # ip -> (port, up30) keep the best-uptime sighting across dumps
    for path in args.dumps:
        if not os.path.exists(path):
            print("warning: %s not found, skipping" % path, file=sys.stderr)
            continue
        for ip, port, up30 in read_dump(path, args.port, args.min_uptime, args.window):
            cur = best.get(ip)
            if cur is None or up30 > cur[1]:
                best[ip] = (port, up30)

    # sort by uptime desc, then address
    nodes = sorted(best.items(), key=lambda kv: (-kv[1][1], str(kv[0])))
    if args.max > 0:
        nodes = nodes[: args.max]

    if not nodes:
        print("No good nodes matched the filters. Has the crawler run long enough?",
              file=sys.stderr)
        sys.exit(1)

    os.makedirs(args.outdir, exist_ok=True)
    txt_path = os.path.join(args.outdir, "nodes_main.txt")
    snip_path = os.path.join(args.outdir, "chainparamsseeds.snippet.h")

    with open(txt_path, "w") as f:
        for ip, (port, _up) in nodes:
            host = "[%s]" % ip if ip.version == 6 else str(ip)
            f.write("%s:%d\n" % (host, port))

    with open(snip_path, "w") as f:
        f.write("// Generated by tools/export-chainparams-seeds.py from live crawler data.\n")
        f.write("// Paste into the chain's src/chainparamsseeds.h to refresh pnSeed6_main[].\n")
        f.write("static SeedSpec6 pnSeed6_main[] = {\n")
        for ip, (port, _up) in nodes:
            f.write(fmt_c_row(ip, port) + "\n")
        f.write("};\n")

    print("Wrote %d seeds:" % len(nodes))
    print("  %s" % txt_path)
    print("  %s" % snip_path)


if __name__ == "__main__":
    main()
