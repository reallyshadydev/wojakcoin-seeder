# multicoin-seeder (DNS seeder / crawler)

A standalone, **multi-chain DNS seeder / network crawler**, forked from
[`sipa/bitcoin-seeder`](https://github.com/sipa/bitcoin-seeder). One binary serves any
number of Bitcoin-family chains, driven by per-chain config presets. Designed to run
several chains on **one server**.

For each chain it does two jobs from a single process:

1. **Crawls** the network. Starting from a few bootstrap seeds it connects to nodes, asks
   for more peers (`getaddr`), and repeats — building a live map. It revisits known nodes
   to track reliability and bans dead/bad ones.
2. **Serves** the healthy nodes over a built-in DNS server. One lookup of the seed
   hostname returns **many** live peer IPs, so a fresh wallet can bootstrap.

It never modifies any coin's source. It only *uses* the IP seeds baked into chainparams as
starting points, and can *produce* refreshed IP seeds you may paste back into a coin's
chainparams later (see [Refreshing chainparams IP seeds](#refreshing-chainparams-ip-seeds)).

## Supported chains (presets in `chains/`)

| Chain      | Magic       | P2P port | Min height | Min proto | Source of params |
|------------|-------------|----------|-----------:|----------:|------------------|
| wojakcoin  | `6f8da579`  | 20759    | 130000     | 70001     | wojakcore chainparams |
| pepecoin   | `c0a0f0e0`  | 33874    | 1080000    | 70003     | pepecoincore + live daemon (`/pepetoshi:1.1.0/`, proto 70016) |

Add more by dropping another file in `chains/` — see [Adding a chain](#adding-a-chain).

## Build

```bash
sudo apt-get install build-essential libboost-all-dev libssl-dev
make
```

Produces the `dnsseed` binary (shared by all chains).

## Run

All control goes through `./seederctl.sh <chain> <command>`:

```bash
./seederctl.sh list                 # show available chain presets
./seederctl.sh pepecoin start       # background crawler for pepecoin
./seederctl.sh wojakcoin start      # ...and wojakcoin, at the same time
./seederctl.sh pepecoin status      # pid + good/tracked counts
./seederctl.sh pepecoin restart
./seederctl.sh pepecoin stop
./seederctl.sh pepecoin run         # foreground (Ctrl-C) — handy for testing / systemd
```

Each chain keeps its own state in `data/<chain>/` (`dnsseed.dat/dump/log/pid`), so any
number of chains run concurrently without interfering. Out of the box each runs
**crawler-only** (no DNS, no domain needed) and writes `data/<chain>/dnsseed.dump`.

To turn a chain into a real **DNS seed**, set `HOST`/`NS`/`MBOX` in its
`chains/<chain>.conf` and restart it (see [Making it a DNS seed](#making-it-a-dns-seed)).

### Keeping chains alive across reboots (optional)

A systemd **template** unit is in [`contrib/seeder@.service`](contrib/seeder@.service) —
the instance name is the chain:

```bash
sudo cp contrib/seeder@.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now seeder@pepecoin
sudo systemctl enable --now seeder@wojakcoin
journalctl -u seeder@pepecoin -f
```

## Adding a chain

1. Copy a preset: `cp chains/pepecoin.conf chains/mycoin.conf`.
2. Fill in from the coin's `src/chainparams.cpp` (mainnet `CMainParams`) and `src/version.h`:
   - `MAGIC` — the four `pchMessageStart[0..3]` bytes concatenated (e.g. `c0a0f0e0`).
   - `P2PORT` — `nDefaultPort`.
   - `SEEDS` — the `vSeeds` hostnames and/or a few known-good node IPs.
   - `MINHEIGHT` — a bit below the current chain tip.
   - `MINVERSION` — the coin's `MIN_PEER_PROTO_VERSION`.
3. `./seederctl.sh mycoin start`.

> **Protocol-version gotcha.** This seeder advertises protocol version `70016` (see
> `PROTOCOL_VERSION` in `serialize.h`). A peer only accepts us if that is `>=` its
> `MIN_PEER_PROTO_VERSION`. 70016 covers the usual Bitcoin/Dogecoin/Litecoin forks; if you
> add a chain whose minimum is higher, bump `PROTOCOL_VERSION` and rebuild. `MINVERSION`
> is the *other* direction — the minimum a peer must advertise for us to *serve* it.

## Making it a DNS seed

A DNS seed only works if a hostname is **delegated** to this server, because the wallet
resolves the hostname and expects `A`/`AAAA` records back. (Bare IPs can't go in a wallet's
`vSeeds`; those belong in the fixed seeds, `pnSeed6_main` — see the export section.)

1. **Delegate** the seed hostname to this server in your DNS zone:

   ```
   seed.pepecoin.org.   IN   NS    vps.pepecoin.org.
   vps.pepecoin.org.    IN   A     203.0.113.10
   ```
   Verify: `dig -t NS seed.pepecoin.org`

2. **Configure** in `chains/pepecoin.conf`:
   ```sh
   HOST="seed.pepecoin.org"
   NS="vps.pepecoin.org"
   MBOX="admin.pepecoin.org"
   ```
   then `./seederctl.sh pepecoin restart`.

3. **Test:** `dig @203.0.113.10 seed.pepecoin.org A`

### Binding port 53 without full root

```bash
sudo setcap 'cap_net_bind_service=+ep' ./dnsseed
```
(the systemd unit already grants `CAP_NET_BIND_SERVICE`), or run on a high port
(`DNSPORT="15353"`) and redirect:
```bash
sudo iptables -t nat -A PREROUTING -p udp --dport 53 -j REDIRECT --to-port 15353
```

### Multi-chain DNS on one server

DNS clients always query port **53**, and one IP can bind port 53 only once. So serving
several chains' DNS from a single box requires **one IP per chain** (bind each chain's
`LISTEN` to its own address, all on port 53). With a single IP you can run only one chain's
DNS on 53; the others can still run as crawlers + IP export, or on non-standard ports
behind your own routing. Crawling itself has no such limit — any number of chains crawl
fine on one IP.

## Refreshing chainparams IP seeds

`tools/export-chainparams-seeds.py` turns a chain's crawler output into IP-seed formats.
`seederctl.sh <chain> export` runs it with the right port automatically:

```bash
./seederctl.sh pepecoin export                      # -> data/pepecoin/export/
./seederctl.sh wojakcoin export --min-uptime 50     # extra args pass through
```

It writes, per chain:
* `nodes_main.txt` — plain `ip:port` list (IPv6 bracketed).
* `chainparamsseeds.snippet.h` — a ready-to-paste `pnSeed6_main[]` C array.

Review the snippet and, if desired, paste it into that coin's `src/chainparamsseeds.h` and
rebuild the wallet. (The tool only generates text; applying it is a separate manual step.)

## File layout

```
dnsseed                         the built binary (all chains)
Makefile  *.cpp  *.h            source (forked sipa/bitcoin-seeder)
seederctl.sh                    ./seederctl.sh <chain> {start|stop|restart|status|run|export}
chains/<chain>.conf             per-chain presets (magic/port/height/version/seeds + DNS)
data/<chain>/                   per-chain runtime state: dnsseed.dat/dump/log/pid + export/
contrib/seeder@.service         systemd template unit (seeder@<chain>)
tools/export-chainparams-seeds.py   crawler dump  ->  chainparams IP seeds
README                          original upstream README (reference)
README.md                       this file
```

## Changes vs. upstream sipa/bitcoin-seeder

* `serialize.h` — `PROTOCOL_VERSION` advertised to peers bumped `60000` → `70016` so
  high-minimum chains (e.g. Pepecoin, min 70003) accept the seeder.
* `db.h` / `db.cpp` / `main.cpp` — `REQUIRE_VERSION` (#define) replaced by a runtime
  `--minversion` flag (`nMinPeerVersion`), so the good-node version filter is per chain.
* `db.h` — `GetRequireHeight()` default lowered to a sane value; per-chain via `--minheight`.
* `main.cpp` / `protocol.*` — magic, port, and seeds are all driven per chain at runtime
  (`--magic`, `--p2port`, `-s`) by the chain presets; built-in defaults kept for wojakcoin.
* `bitcoin.cpp` — subversion string → `/multicoin-seeder:0.01/`.
* Added: `chains/` presets, chain-aware `seederctl.sh`, `tools/export-chainparams-seeds.py`,
  `contrib/seeder@.service`.
