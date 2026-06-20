#!/usr/bin/env python3
"""
Scaffold a new chain section in seeder.conf straight from a coin's source tree.

The seeder is chain-agnostic: every coin is just a [section] in seeder.conf with its
network magic, P2P port, seed hosts and (optionally) a minimum height/version. This
tool reads those values out of a Bitcoin-family coin's source so you don't have to
hunt through chainparams.cpp by hand.

Usage:
  tools/add-chain.py <name> --src /path/to/coin-source [options]

It parses, from the MAINNET (CMainParams) section:
  <src>/src/chainparams.cpp  -> magic (pchMessageStart), port (nDefaultPort), DNS seeds
  <src>/src/version.h        -> minversion (MIN_PEER_PROTO_VERSION)

Options:
  --src DIR          coin source tree (expects DIR/src/chainparams.cpp). Required
                     unless --magic/--port are given explicitly.
  --magic HEX        override/skip parsing: 8 hex chars, e.g. c0a0f0e0
  --port N           override/skip parsing: P2P port
  --seeds "a b c"    extra bootstrap seeds (hosts/IPs), space-separated
  --minheight N      minimum good-node height (omit to disable the height filter)
  --minversion N     minimum peer protocol version (omit to disable)
  --cli "CMD"        a working <coin>-cli command; queries getblockcount for a
                     sensible minheight and getpeerinfo for a few live seed IPs
  --conf FILE        seeder.conf to append to (default: seeder.conf next to this tool)
  --print            print the section to stdout instead of writing the file

Examples:
  tools/add-chain.py dogecoin --src /root/dogecoin-1.14.9
  tools/add-chain.py pepecoin --src /root/pepecoincore \\
      --cli "/root/pepecoin/bin/pepecoin-cli -rpcport=33873 -rpcuser=u -rpcpassword=p"
"""
import argparse
import json
import os
import re
import subprocess
import sys


def die(msg):
    print("error: " + msg, file=sys.stderr)
    sys.exit(1)


def mainnet_block(text):
    """Return the slice of chainparams.cpp covering the mainnet (CMainParams) block."""
    m = re.search(r'strNetworkID\s*=\s*"main"', text)
    if not m:
        return text  # fall back to whole file
    start = m.start()
    # end at the next network's strNetworkID (test/regtest), if any
    nxt = re.search(r'strNetworkID\s*=\s*"(?:test|regtest)"', text[start + 1:])
    end = start + 1 + nxt.start() if nxt else len(text)
    return text[start:end]


def parse_magic(block):
    bytes_by_idx = {}
    for mm in re.finditer(r'pchMessageStart\s*\[\s*(\d)\s*\]\s*=\s*0x([0-9a-fA-F]{1,2})', block):
        bytes_by_idx[int(mm.group(1))] = int(mm.group(2), 16)
    if len(bytes_by_idx) < 4:
        return None
    return "".join("%02x" % bytes_by_idx[i] for i in range(4))


def parse_port(block):
    m = re.search(r'nDefaultPort\s*=\s*(\d+)', block)
    return int(m.group(1)) if m else None


def parse_seeds(block):
    seeds = []
    # old style: CDNSSeedData("name", "host")  -> take the resolvable host (2nd arg)
    for mm in re.finditer(r'CDNSSeedData\(\s*"[^"]*"\s*,\s*"([^"]+)"', block):
        seeds.append(mm.group(1))
    # newer style: vSeeds.emplace_back("host");
    for mm in re.finditer(r'emplace_back\(\s*"([^"]+)"', block):
        seeds.append(mm.group(1))
    # de-dupe, keep order
    seen, out = set(), []
    for s in seeds:
        if s not in seen:
            seen.add(s)
            out.append(s)
    return out


def parse_minversion(src):
    vh = os.path.join(src, "src", "version.h")
    if not os.path.exists(vh):
        return None
    text = open(vh, errors="ignore").read()
    m = re.search(r'MIN_PEER_PROTO_VERSION\s*=\s*([A-Za-z0-9_]+)', text)
    if not m:
        return None
    val = m.group(1)
    if val.isdigit():
        return int(val)
    # resolve a named constant one level (e.g. = GETHEADERS_VERSION)
    m2 = re.search(r'\b' + re.escape(val) + r'\s*=\s*(\d+)', text)
    return int(m2.group(1)) if m2 else None


def cli_query(cli, *args):
    try:
        out = subprocess.check_output(cli.split() + list(args),
                                      stderr=subprocess.DEVNULL, timeout=20)
        return out.decode().strip()
    except Exception as e:
        print("warning: cli %s failed: %s" % (" ".join(args), e), file=sys.stderr)
        return None


def live_seeds_from_cli(cli, port, limit=5):
    raw = cli_query(cli, "getpeerinfo")
    if not raw:
        return []
    try:
        peers = json.loads(raw)
    except Exception:
        return []
    out = []
    for p in peers:
        addr = p.get("addr", "")
        # keep only peers on the default listening port (likely reachable nodes)
        if addr.endswith(":%d" % port):
            host = addr.rsplit(":", 1)[0]
            if host and host not in out:
                out.append(host)
        if len(out) >= limit:
            break
    return out


def main():
    ap = argparse.ArgumentParser(add_help=True, description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name")
    ap.add_argument("--src")
    ap.add_argument("--magic")
    ap.add_argument("--port", type=int)
    ap.add_argument("--seeds", default="")
    ap.add_argument("--minheight", type=int)
    ap.add_argument("--minversion", type=int)
    ap.add_argument("--cli")
    ap.add_argument("--conf")
    ap.add_argument("--print", dest="just_print", action="store_true")
    args = ap.parse_args()

    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    conf = args.conf or os.path.join(here, "seeder.conf")

    magic = args.magic
    port = args.port
    seeds = []
    minversion = args.minversion

    if args.src:
        cp = os.path.join(args.src, "src", "chainparams.cpp")
        if not os.path.exists(cp):
            die("no chainparams.cpp at %s" % cp)
        block = mainnet_block(open(cp, errors="ignore").read())
        magic = magic or parse_magic(block)
        if port is None:
            port = parse_port(block)
        seeds = parse_seeds(block)
        if minversion is None:
            minversion = parse_minversion(args.src)

    if not magic or len(magic) != 8 or re.search(r'[^0-9a-fA-F]', magic):
        die("could not determine 8-hex-char magic (got %r); pass --magic" % magic)
    if not port:
        die("could not determine P2P port; pass --port")

    # extra seeds from the command line
    seeds += [s for s in args.seeds.split() if s]

    minheight = args.minheight
    if args.cli:
        if minheight is None:
            bc = cli_query(args.cli, "getblockcount")
            if bc and bc.isdigit():
                minheight = int(int(bc) * 0.98)  # ~2% below the tip
        seeds += [s for s in live_seeds_from_cli(args.cli, port) if s not in seeds]

    if not seeds:
        print("warning: no seeds found; the chain won't bootstrap until you add some",
              file=sys.stderr)

    # build the section
    lines = ["", "[%s]" % args.name,
             "magic      = %s" % magic.lower(),
             "port       = %d" % port]
    if minheight:
        lines.append("minheight  = %d" % minheight)
    if minversion:
        lines.append("minversion = %d" % minversion)
    lines.append("seeds      = %s" % " ".join(seeds))
    lines += ["host       =", "ns         =", "mbox       ="]
    section = "\n".join(lines) + "\n"

    if args.just_print:
        print(section)
        return

    existing = open(conf).read() if os.path.exists(conf) else ""
    if re.search(r'(?m)^\s*\[%s\]\s*$' % re.escape(args.name), existing):
        die("chain [%s] already exists in %s" % (args.name, conf))
    with open(conf, "a") as f:
        f.write(section)
    print("Added [%s] to %s" % (args.name, conf))
    print(section)
    print("Next: ./seederctl.sh restart    (then ./seederctl.sh status)")


if __name__ == "__main__":
    main()
