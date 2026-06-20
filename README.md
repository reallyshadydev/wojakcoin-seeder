# multicoin-seeder (DNS seeder / crawler)

A standalone, **multi-chain DNS seeder / network crawler**, forked from
[`sipa/bitcoin-seeder`](https://github.com/sipa/bitcoin-seeder). **One process** crawls
(and optionally DNS-serves) any number of Bitcoin-family chains at once, driven by a single
config file. Built to run several coins on **one server, in one process**.

For each chain it does two jobs:

1. **Crawls** the network — starting from a few bootstrap seeds it connects to nodes, asks
   for more peers (`getaddr`), and repeats, tracking which nodes stay reachable and banning
   dead/bad ones.
2. **Serves** the healthy nodes over a built-in DNS server. One lookup of the seed hostname
   returns **many** live peer IPs so a fresh wallet can bootstrap.

It never modifies any coin's source. It only *uses* the IP seeds baked into chainparams as
starting points, and can *produce* refreshed IP seeds to paste back into a coin's chainparams
later (see [Refreshing chainparams IP seeds](#refreshing-chainparams-ip-seeds)).

## How one process runs many chains

The upstream seeder keeps the network magic, port, and height as global state, so it can
only do one chain. Here those are **thread-local**: each chain gets its own database and its
own pool of crawler/DNS/dumper threads inside the single process, and every thread points the
thread-local params at its own chain (`applyChain()` in `main.cpp`). Result: `wojakcoin` and
`pepecoin` crawl simultaneously in one PID without interfering.

## Supported chains (sections in `seeder.conf`)

| Chain      | Magic       | P2P port | Min height | Min proto | Source of params |
|------------|-------------|----------|-----------:|----------:|------------------|
| wojakcoin  | `6f8da579`  | 20759    | 130000     | 70001     | wojakcore chainparams |
| pepecoin   | `c0a0f0e0`  | 33874    | 1080000    | 70003     | pepecoincore + live daemon (`/pepetoshi:1.1.0/`, proto 70016) |

## Build

```bash
sudo apt-get install build-essential libboost-all-dev libssl-dev
make
```

Produces the `dnsseed` binary.

## Run

One process, all chains, via `./seederctl.sh`:

```bash
./seederctl.sh start          # ONE process serving every chain in seeder.conf
./seederctl.sh status         # per-chain good/tracked node counts
./seederctl.sh restart
./seederctl.sh stop
./seederctl.sh run            # foreground (Ctrl-C) — for testing / systemd
```

Each chain keeps its own state in `data/<chain>/` (`dnsseed.dat/dump`), and the process logs
to `data/dnsseed.log`. Out of the box every chain runs **crawler-only** (no DNS, no domain
needed). Under the hood this is just `dnsseed -c seeder.conf --datadir data`.

### Keep it running across reboots (optional)

[`contrib/seeder.service`](contrib/seeder.service) is a systemd unit for the single process:

```bash
sudo cp contrib/seeder.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now seeder
journalctl -u seeder -f
```

## Configuring chains (`seeder.conf`)

One `[section]` per chain; keys below. Defaults: `dnsport=53`, `listen=::`.

```ini
[pepecoin]
magic      = c0a0f0e0      # 4 message-start bytes from chainparams.cpp, concatenated
port       = 33874         # nDefaultPort
minheight  = 1080000       # a bit below the chain tip
minversion = 70003         # the coin's MIN_PEER_PROTO_VERSION
seeds      = seeds.pepecoin.org seeds.pepeblocks.com 65.109.147.252
host       =               # set host+ns+mbox to also serve DNS (see below)
ns         =
mbox       =
```

### Adding a chain

1. Add a new `[mycoin]` section to `seeder.conf`.
2. Fill `magic` / `port` from the coin's `src/chainparams.cpp` (mainnet) and `minversion`
   from its `src/version.h` (`MIN_PEER_PROTO_VERSION`); set `seeds` and a `minheight` below
   the current tip.
3. `./seederctl.sh restart`.

> **Protocol-version gotcha.** The seeder advertises protocol version `70016`
> (`PROTOCOL_VERSION` in `serialize.h`). A peer accepts us only if that is `>=` its
> `MIN_PEER_PROTO_VERSION`. 70016 covers the usual Bitcoin/Dogecoin/Litecoin forks; if a new
> chain's minimum is higher, bump `PROTOCOL_VERSION` and rebuild. `minversion` is the other
> direction — the minimum a peer must advertise for us to *serve* it.

## Making a chain a DNS seed

A DNS seed only works if a hostname is **delegated** to this server (the wallet resolves the
hostname and expects `A`/`AAAA` records). Bare IPs can't go in a wallet's `vSeeds`; those
belong in the fixed seeds, `pnSeed6_main` — see the export section.

1. **Delegate** the seed hostname to this server:
   ```
   seed.pepecoin.org.   IN   NS    vps.pepecoin.org.
   vps.pepecoin.org.    IN   A     203.0.113.10
   ```
2. **Configure** that chain's section: `host = seed.pepecoin.org`, `ns = vps.pepecoin.org`,
   `mbox = admin.pepecoin.org`, then `./seederctl.sh restart`.
3. **Test:** `dig @203.0.113.10 seed.pepecoin.org A`

Bind port 53 without full root via `setcap 'cap_net_bind_service=+ep' ./dnsseed` (the systemd
unit already grants it), or set `dnsport = 15353` and redirect with iptables.

### Multi-chain DNS on one server

DNS clients always query port **53**, and one IP can bind port 53 only once — even within one
process. To DNS-serve several chains you need **one IP per chain** (set each chain's `listen`
to its own address, all on port 53). With a single IP, serve one chain's DNS on 53 and run the
rest as crawler + IP export. Crawling has no such limit — all chains crawl fine on one IP.

## Refreshing chainparams IP seeds

`./seederctl.sh export <chain>` turns a chain's crawler output into IP-seed formats (it reads
the chain's port from `seeder.conf` automatically):

```bash
./seederctl.sh export pepecoin                  # -> data/pepecoin/export/
./seederctl.sh export wojakcoin --min-uptime 50 # extra args pass through to the tool
```

Per chain it writes:
* `nodes_main.txt` — plain `ip:port` list (IPv6 bracketed).
* `chainparamsseeds.snippet.h` — a ready-to-paste `pnSeed6_main[]` C array.

Review the snippet and, if desired, paste it into that coin's `src/chainparamsseeds.h` and
rebuild the wallet. (The tool only generates text; applying it is a separate manual step.)

## File layout

```
dnsseed                       the built binary (serves all chains)
Makefile  *.cpp  *.h          source (forked sipa/bitcoin-seeder, made multi-chain)
seeder.conf                   chains config: one [section] per coin
seederctl.sh                  start|stop|restart|status|run, and export <chain>
data/<chain>/                 per-chain state: dnsseed.dat/dump + export/; data/dnsseed.log
contrib/seeder.service        optional systemd unit (single process)
tools/export-chainparams-seeds.py   crawler dump  ->  chainparams IP seeds
README / README.md            upstream reference / this file
```

## Changes vs. upstream sipa/bitcoin-seeder

* **Multi-chain in one process:** `pchMessageStart`, `nDefaultP2Port`, `nMinimumHeight`,
  `nMinPeerVersion` made `thread_local`; per-chain `CAddrDb` + thread pools; `main.cpp` loads
  a multi-chain config and launches everything (`CChain`, `applyChain()`, `LoadConfig()`).
* `serialize.h` — advertised `PROTOCOL_VERSION` `60000` → `70016` so high-minimum chains
  (e.g. Pepecoin, min 70003) accept the seeder.
* `db.h`/`main.cpp` — `REQUIRE_VERSION` #define → per-chain `minversion`.
* `GetRequireHeight()` default lowered; per-chain `minheight`.
* `bitcoin.cpp` — subversion string → `/multicoin-seeder:0.01/`.
* Added: `seeder.conf`, `seederctl.sh`, `tools/export-chainparams-seeds.py`,
  `contrib/seeder.service`.
