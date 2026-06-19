# wojakcoin-seeder

A standalone **DNS seeder / network crawler** for WojakCoin, forked from
[`sipa/bitcoin-seeder`](https://github.com/sipa/bitcoin-seeder) and retuned for the
WojakCoin P2P network. Designed to run on **one server**.

It does two jobs from a single process:

1. **Crawls** the WojakCoin network. Starting from a few bootstrap seeds, it connects
   to nodes, asks them for more peers (`getaddr`), and repeats — building a live map of
   the network. It revisits known nodes to track reliability and bans dead/bad ones.
2. **Serves** the healthy nodes over a built-in DNS server. A single lookup of your seed
   hostname returns **many** live peer IPs, so a fresh `wojakcoind` can bootstrap. (One
   hostname returning many peers *is* the standard DNS-seed design — you do not need
   several hostnames or several servers.)

It never modifies `wojakcore`. It only *uses* the IP seeds baked into chainparams as
starting points, and can *produce* refreshed IP seeds you may paste back into chainparams
later (see [Refreshing the chainparams IP seeds](#refreshing-the-chainparams-ip-seeds)).

## WojakCoin network parameters (already baked in)

Taken from `wojakcore/src/chainparams.cpp`; each is a built-in default and can be
overridden on the command line.

| Parameter            | Mainnet            | Testnet            | Override        |
|----------------------|--------------------|--------------------|-----------------|
| Message-start (magic)| `6f 8d a5 79`      | `4d aa 61 f9`      | `--magic`       |
| P2P port             | `20759`            | `30759`            | `--p2port`      |
| Min "good" height    | `130000`           | `1`                | `--minheight`   |
| Min protocol version | `70001` (net is 70012) | —              | (compile-time)  |
| Bootstrap seeds      | `103.133.25.201`, `159.223.90.59`, `207.244.232.43`, `wojak-seed.s3na.xyz` | none | `-s` |

> **`--minheight`:** a node is only advertised once it reports a height ≥ this value.
> The wojak tip was ~139k in mid-2026, so the default is 130000. If you ever see zero
> good nodes, your `--minheight` is above the current tip — lower it.

## Build

```bash
sudo apt-get install build-essential libboost-all-dev libssl-dev
make
```

Produces the `dnsseed` binary.

## Run (single server)

Everything is driven by `./seederctl.sh` and configured in `./seeder.conf`. Runtime
state (`dnsseed.dat/dump/log`) lives in `./data/`.

```bash
./seederctl.sh start      # background
./seederctl.sh status     # pid + good/tracked node counts
./seederctl.sh restart
./seederctl.sh stop
./seederctl.sh run        # foreground (Ctrl-C to quit) — handy for testing
```

Out of the box (no `HOST`/`NS` in `seeder.conf`) it runs **crawler-only**: it maps the
network and writes `data/dnsseed.dump`, with no DNS server and no domain required. That
alone is enough to feed the [chainparams IP-seed export](#refreshing-the-chainparams-ip-seeds).

To make it a real **DNS seed**, set `HOST`, `NS`, `MBOX` in `seeder.conf` (see below) and
`./seederctl.sh restart`.

### Keeping it alive across reboots (optional)

An optional systemd unit is in [`contrib/wojakcoin-seeder.service`](contrib/wojakcoin-seeder.service):

```bash
sudo cp contrib/wojakcoin-seeder.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now wojakcoin-seeder
journalctl -u wojakcoin-seeder -f
```

## Making it a real DNS seed

A DNS seed only works if a hostname is **delegated** to this server, because `wojakcoind`
resolves the hostname and expects `A`/`AAAA` records back. (Bare IPs can't go in a wallet's
`vSeeds` — those belong in the fixed seeds, `pnSeed6_main`; see the export section.)

### 1. Delegate one hostname to this server

In the DNS zone of your domain, add an **NS** record for the seed hostname pointing at this
server, plus an `A`/glue record for it:

```
seed.wojakcoin.org.   IN   NS    vps.wojakcoin.org.
vps.wojakcoin.org.    IN   A     203.0.113.10
```

Verify: `dig -t NS seed.wojakcoin.org`

### 2. Configure and start

In `seeder.conf`:

```sh
HOST="seed.wojakcoin.org"
NS="vps.wojakcoin.org"
MBOX="admin.wojakcoin.org"
```

```bash
./seederctl.sh restart
```

### 3. Test

```bash
dig @203.0.113.10 seed.wojakcoin.org        # A records of live nodes
dig @203.0.113.10 AAAA seed.wojakcoin.org   # IPv6 nodes
```

### Binding port 53 without full root

Port 53 is privileged. Either grant the capability:

```bash
sudo setcap 'cap_net_bind_service=+ep' ./dnsseed
```

(the provided systemd unit already sets `AmbientCapabilities=CAP_NET_BIND_SERVICE`), or run
on a high port and redirect:

```bash
# in seeder.conf: DNSPORT="15353"
sudo iptables -t nat -A PREROUTING -p udp --dport 53 -j REDIRECT --to-port 15353
```

### Wiring it into the wallet

Once `seed.wojakcoin.org` resolves, the WojakCoin maintainers would add it to
`wojakcore/src/chainparams.cpp` (`vSeeds.push_back(CDNSSeedData(...))`) in a future
release. That edit lives in the wallet repo, not here.

## Refreshing the chainparams IP seeds

`tools/export-chainparams-seeds.py` turns crawler output into the exact IP-seed formats
WojakCoin uses, so the **IP seeds embedded in chainparams** can be refreshed from live data.
It reads `data/dnsseed.dump`, keeps good nodes, de-duplicates, and writes:

* `nodes_main.txt` — plain `ip:port` list (IPv6 bracketed).
* `chainparamsseeds.snippet.h` — a ready-to-paste `pnSeed6_main[]` C array matching
  `wojakcore/src/chainparamsseeds.h`.

```bash
# good nodes from the running crawler
./tools/export-chainparams-seeds.py data/dnsseed.dump --outdir export

# stricter: >=50% reliability over the 7-day window, cap at 30 seeds
./tools/export-chainparams-seeds.py --min-uptime 50 --window 7d --max 30 \
    data/dnsseed.dump --outdir export
```

Review `export/chainparamsseeds.snippet.h`, and if desired paste it into the wallet repo's
`chainparamsseeds.h` and rebuild. (This tool only generates text; applying it to `wojakcore`
is a separate, manual decision.)

## File layout

```
dnsseed                       the built binary
Makefile  *.cpp  *.h          source (forked sipa/bitcoin-seeder, retuned for wojak)
seederctl.sh                  start | stop | restart | status | run
seeder.conf                   configuration (crawler-only vs DNS-seed mode)
data/                         runtime state: dnsseed.dat / dnsseed.dump / dnsseed.log
contrib/wojakcoin-seeder.service   optional systemd unit
tools/export-chainparams-seeds.py  crawler dump  ->  chainparams IP seeds
README                        original upstream README (reference)
README.md                     this file
```

## WojakCoin-specific changes vs. upstream

* `protocol.cpp` — default magic bytes → `6f 8d a5 79`.
* `protocol.h` — default P2P port → `20759` / `30759`.
* `db.h` — `GetRequireHeight()` default → `130000` (was 350000, which is above the wojak
  tip and rejected every node).
* `main.cpp` — built-in mainnet seeds → wojak chainparams IPs + `wojak-seed.s3na.xyz`;
  testnet magic → `4d aa 61 f9`; branding.
* `bitcoin.cpp` — subversion string → `/wojakcoin-seeder:0.01/`.
