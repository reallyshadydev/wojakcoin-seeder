#include <algorithm>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <atomic>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include "bitcoin.h"
#include "db.h"

using namespace std;

bool fTestNet = false;

class CDnsSeedOpts {
public:
  int nThreads;
  int nPort;
  int nP2Port;
  int nMinimumHeight;
  int nMinVersion;
  int nDnsThreads;
  int fUseTestNet;
  int fWipeBan;
  int fWipeIgnore;
  const char *mbox;
  const char *ns;
  const char *host;
  const char *tor;
  const char *ip_addr;
  const char *ipv4_proxy;
  const char *ipv6_proxy;
  const char *magic;
  const char *config;
  const char *datadir;
  std::vector<string> vSeeds;
  std::set<uint64_t> filter_whitelist;

  CDnsSeedOpts() : nThreads(96), nDnsThreads(4), ip_addr("::"), nPort(53), nP2Port(0), nMinimumHeight(0), nMinVersion(0), mbox(NULL), ns(NULL), host(NULL), tor(NULL), fUseTestNet(false), fWipeBan(false), fWipeIgnore(false), ipv4_proxy(NULL), ipv6_proxy(NULL), magic(NULL), config("seeder.conf"), datadir("data") {}

  void ParseCommandLine(int argc, char **argv) {
    static const char *help = "multicoin-seeder (multi-chain DNS seeder / crawler)\n"
                              "Usage: %s -c <config> [-t <threads>] [-d <dnsthreads>] [--datadir <dir>]\n"
                              "\n"
                              "Chains (magic/port/seeds/host/...) are defined in the config file, one\n"
                              "[section] per chain; all run together in this single process.\n"
                              "\n"
                              "Options:\n"
                              "-c <config>     Config file with [chain] sections (default seeder.conf)\n"
                              "--datadir <dir> Base dir for per-chain state (default data)\n"
                              "-s <seed>       Seed node to collect peers from (replaces default)\n"
                              "-h <host>       Hostname of the DNS seed\n"
                              "-n <ns>         Hostname of the nameserver\n"
                              "-m <mbox>       E-Mail address reported in SOA records\n"
                              "-t <threads>    Number of crawlers to run in parallel (default 96)\n"
                              "-d <threads>    Number of DNS server threads (default 4)\n"
                              "-a <address>    Address to listen on (default ::)\n"
                              "-p <port>       UDP port to listen on (default 53)\n"
                              "-o <ip:port>    Tor proxy IP/Port\n"
                              "-i <ip:port>    IPV4 SOCKS5 proxy IP/Port\n"
                              "-k <ip:port>    IPV6 SOCKS5 proxy IP/Port\n"
                              "-w f1,f2,...    Allow these flag combinations as filters\n"
                              "--p2port <port> P2P port to connect to\n"
                              "--magic <hex>   Magic string/network prefix\n"
                              "--minheight <n> Minimum height of block chain\n"
                              "--minversion <n> Minimum peer protocol version to serve\n"
                              "--testnet       Use testnet\n"
                              "--wipeban       Wipe list of banned nodes\n"
                              "--wipeignore    Wipe list of ignored nodes\n"
                              "-?, --help      Show this text\n"
                              "\n";
    bool showHelp = false;

    while(1) {
      static struct option long_options[] = {
        {"seed", required_argument, 0, 's'},
        {"host", required_argument, 0, 'h'},
        {"ns",   required_argument, 0, 'n'},
        {"mbox", required_argument, 0, 'm'},
        {"threads", required_argument, 0, 't'},
        {"dnsthreads", required_argument, 0, 'd'},
        {"address", required_argument, 0, 'a'},
        {"port", required_argument, 0, 'p'},
        {"onion", required_argument, 0, 'o'},
        {"proxyipv4", required_argument, 0, 'i'},
        {"proxyipv6", required_argument, 0, 'k'},
        {"filter", required_argument, 0, 'w'},
        {"p2port", required_argument, 0, 'b'},
        {"magic", required_argument, 0, 'q'},
        {"minheight", required_argument, 0, 'x'},
        {"minversion", required_argument, 0, 'v'},
        {"config", required_argument, 0, 'c'},
        {"datadir", required_argument, 0, 'D'},
        {"testnet", no_argument, &fUseTestNet, 1},
        {"wipeban", no_argument, &fWipeBan, 1},
        {"wipeignore", no_argument, &fWipeBan, 1},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
      };
      int option_index = 0;
      int c = getopt_long(argc, argv, "s:h:n:m:t:a:p:d:o:i:k:w:b:q:x:v:c:D:", long_options, &option_index);
      if (c == -1) break;
      switch (c) {
        case 's': {
          vSeeds.emplace_back(optarg);
          break;
        }

        case 'h': {
          host = optarg;
          break;
        }
        
        case 'm': {
          mbox = optarg;
          break;
        }
        
        case 'n': {
          ns = optarg;
          break;
        }
        
        case 't': {
          int n = strtol(optarg, NULL, 10);
          if (n > 0 && n < 1000) nThreads = n;
          break;
        }

        case 'd': {
          int n = strtol(optarg, NULL, 10);
          if (n > 0 && n < 1000) nDnsThreads = n;
          break;
        }

        case 'a': {
          if (strchr(optarg, ':')==NULL) {
            char* ip4_addr = (char*) malloc(strlen(optarg)+8);
            strcpy(ip4_addr, "::FFFF:");
            strcat(ip4_addr, optarg);
            ip_addr = ip4_addr;
          } else {
            ip_addr = optarg;
          }
          break;
        }

        case 'p': {
          int p = strtol(optarg, NULL, 10);
          if (p > 0 && p < 65536) nPort = p;
          break;
        }

        case 'o': {
          tor = optarg;
          break;
        }

        case 'i': {
          ipv4_proxy = optarg;
          break;
        }

        case 'k': {
          ipv6_proxy = optarg;
          break;
        }

        case 'w': {
          char* ptr = optarg;
          while (*ptr != 0) {
            unsigned long l = strtoul(ptr, &ptr, 0);
            if (*ptr == ',') {
                ptr++;
            } else if (*ptr != 0) {
                break;
            }
            filter_whitelist.insert(l);
          }
          break;
        }

        case 'b': {
          int p = strtol(optarg, NULL, 10);
          if (p > 0 && p < 65536) nP2Port = p;
          break;
        }

        case 'q': {
          long int n;
          unsigned int c;
          if (strlen(optarg)!=8) {
            break; /* must be 4 hex-encoded bytes */
          }
          n = strtol(optarg, NULL, 16);
          if (n==0 && strcmp(optarg, "00000000")) {
            break; /* hex decode failed */
          }
          magic = optarg;
          break;
        }

        case 'x': {
          int n = strtol(optarg, NULL, 10);
          if (n > 0 && n <= 0x7fffffff) nMinimumHeight = n;
          break;
        }

        case 'v': {
          int n = strtol(optarg, NULL, 10);
          if (n > 0 && n <= 0x7fffffff) nMinVersion = n;
          break;
        }

        case 'c': {
          config = optarg;
          break;
        }

        case 'D': {
          datadir = optarg;
          break;
        }

        case '?': {
          showHelp = true;
          break;
        }
      }
    }
    if (filter_whitelist.empty()) {
        filter_whitelist.insert(NODE_NETWORK); // x1
        filter_whitelist.insert(NODE_NETWORK | NODE_BLOOM); // x5
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS); // x9
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_COMPACT_FILTERS); // x49
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_P2P_V2); // x809
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_P2P_V2 | NODE_COMPACT_FILTERS); //x849
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_BLOOM); // xd
        filter_whitelist.insert(NODE_NETWORK_LIMITED); // x400
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_BLOOM); // x404
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS); // x408
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_COMPACT_FILTERS); // x448
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_P2P_V2); // xc08
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_P2P_V2 | NODE_COMPACT_FILTERS); // xc48
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_BLOOM); // x40c
    }
    if (host != NULL && ns == NULL) showHelp = true;
    if (showHelp) fprintf(stderr, help, argv[0]);
  }
};

#include "dns.h"

class CDnsThread; // forward decl (CChain holds pointers to these)

// One crawled network. All per-chain state lives here so a single process can run
// several chains at once; each chain gets its own db and its own worker threads.
struct CChain {
  std::string name;
  unsigned char magic[4];
  unsigned short port;
  int minHeight;
  int minVersion;
  std::vector<std::string> seeds;
  std::string host, ns, mbox, listen;
  int dnsPort;
  int nThreads;
  int nDnsThreads;
  std::string datadir;            // <basedir>/<name>, holds dnsseed.dat/dump/log
  CAddrDb db;
  std::vector<CDnsThread*> dnsThreads;
  std::set<uint64_t> filter_whitelist;
  CChain() : port(0), minHeight(0), minVersion(0), dnsPort(53), nThreads(64), nDnsThreads(4) {
    memset(magic, 0, sizeof(magic));
  }
};

// Point the thread_local network params at this chain. Call once at the top of every
// thread that touches a chain (crawler/dns/dumper/seeder) before using its db.
static void applyChain(const CChain* c) {
  memcpy(pchMessageStart, c->magic, sizeof(pchMessageStart));
  nDefaultP2Port = c->port;
  nMinimumHeight = c->minHeight;
  nMinPeerVersion = c->minVersion;
}

std::vector<CChain*> g_chains;

extern "C" void* ThreadCrawler(void* data) {
  CChain* chain = (CChain*)data;
  applyChain(chain);
  do {
    std::vector<CServiceResult> ips;
    int wait = 5;
    chain->db.GetMany(ips, 16, wait);
    int64 now = time(NULL);
    if (ips.empty()) {
      wait *= 1000;
      wait += rand() % (500 * chain->nThreads);
      Sleep(wait);
      continue;
    }
    vector<CAddress> addr;
    for (int i=0; i<ips.size(); i++) {
      CServiceResult &res = ips[i];
      res.nBanTime = 0;
      res.nClientV = 0;
      res.nHeight = 0;
      res.strClientV = "";
      res.services = 0;
      bool getaddr = res.ourLastSuccess + 86400 < now;
      res.fGood = TestNode(res.service,res.nBanTime,res.nClientV,res.strClientV,res.nHeight,getaddr ? &addr : NULL, res.services);
    }
    chain->db.ResultMany(ips);
    chain->db.Add(addr);
  } while(1);
  return nullptr;
}

extern "C" int GetIPList(void *thread, char *requestedHostname, addr_t *addr, int max, int ipv4, int ipv6);

class CDnsThread {
public:
  struct FlagSpecificData {
      int nIPv4, nIPv6;
      std::vector<addr_t> cache;
      time_t cacheTime;
      unsigned int cacheHits;
      FlagSpecificData() : nIPv4(0), nIPv6(0), cacheTime(0), cacheHits(0) {}
  };

  dns_opt_t dns_opt; // must be first
  const int id;
  CChain* chain;
  std::map<uint64_t, FlagSpecificData> perflag;
  std::atomic<uint64_t> dbQueries;
  std::set<uint64_t> filterWhitelist;

  void cacheHit(uint64_t requestedFlags, bool force = false) {
    static bool nets[NET_MAX] = {};
    if (!nets[NET_IPV4]) {
        nets[NET_IPV4] = true;
        nets[NET_IPV6] = true;
    }
    time_t now = time(NULL);
    FlagSpecificData& thisflag = perflag[requestedFlags];
    thisflag.cacheHits++;
    if (force || thisflag.cacheHits * 400 > (thisflag.cache.size()*thisflag.cache.size()) || (thisflag.cacheHits*thisflag.cacheHits * 20 > thisflag.cache.size() && (now - thisflag.cacheTime > 5))) {
      set<CNetAddr> ips;
      chain->db.GetIPs(ips, requestedFlags, 1000, nets);
      dbQueries++;
      thisflag.cache.clear();
      thisflag.nIPv4 = 0;
      thisflag.nIPv6 = 0;
      thisflag.cache.reserve(ips.size());
      for (set<CNetAddr>::iterator it = ips.begin(); it != ips.end(); it++) {
        struct in_addr addr;
        struct in6_addr addr6;
        if ((*it).GetInAddr(&addr)) {
          addr_t a;
          a.v = 4;
          memcpy(&a.data.v4, &addr, 4);
          thisflag.cache.push_back(a);
          thisflag.nIPv4++;
        } else if ((*it).GetIn6Addr(&addr6)) {
          addr_t a;
          a.v = 6;
          memcpy(&a.data.v6, &addr6, 16);
          thisflag.cache.push_back(a);
          thisflag.nIPv6++;
        }
      }
      thisflag.cacheHits = 0;
      thisflag.cacheTime = now;
    }
  }

  CDnsThread(CChain* chainIn, int idIn) : id(idIn), chain(chainIn) {
    dns_opt.host = chain->host.c_str();
    dns_opt.ns = chain->ns.c_str();
    dns_opt.mbox = chain->mbox.c_str();
    dns_opt.datattl = 3600;
    dns_opt.nsttl = 40000;
    dns_opt.cb = GetIPList;
    dns_opt.addr = chain->listen.c_str();
    dns_opt.port = chain->dnsPort;
    dns_opt.nRequests = 0;
    dbQueries = 0;
    perflag.clear();
    filterWhitelist = chain->filter_whitelist;
  }

  void run() {
    dnsserver(&dns_opt);
  }
};

extern "C" int GetIPList(void *data, char *requestedHostname, addr_t* addr, int max, int ipv4, int ipv6) {
  CDnsThread *thread = (CDnsThread*)data;

  uint64_t requestedFlags = 0;
  int hostlen = strlen(requestedHostname);
  if (hostlen > 1 && requestedHostname[0] == 'x' && requestedHostname[1] != '0') {
    char *pEnd;
    uint64_t flags = (uint64_t)strtoull(requestedHostname+1, &pEnd, 16);
    if (*pEnd == '.' && pEnd <= requestedHostname+17 && std::find(thread->filterWhitelist.begin(), thread->filterWhitelist.end(), flags) != thread->filterWhitelist.end())
      requestedFlags = flags;
    else
      return 0;
  }
  else if (strcasecmp(requestedHostname, thread->dns_opt.host))
    return 0;
  thread->cacheHit(requestedFlags);
  auto& thisflag = thread->perflag[requestedFlags];
  unsigned int size = thisflag.cache.size();
  unsigned int maxmax = (ipv4 ? thisflag.nIPv4 : 0) + (ipv6 ? thisflag.nIPv6 : 0);
  if (max > size)
    max = size;
  if (max > maxmax)
    max = maxmax;
  int i=0;
  while (i<max) {
    int j = i + (rand() % (size - i));
    do {
        bool ok = (ipv4 && thisflag.cache[j].v == 4) ||
                  (ipv6 && thisflag.cache[j].v == 6);
        if (ok) break;
        j++;
        if (j==size)
            j=i;
    } while(1);
    addr[i] = thisflag.cache[j];
    thisflag.cache[j] = thisflag.cache[i];
    thisflag.cache[i] = addr[i];
    i++;
  }
  return max;
}

extern "C" void* ThreadDNS(void* arg) {
  CDnsThread *thread = (CDnsThread*)arg;
  applyChain(thread->chain);
  thread->run();
  return nullptr;
}

int StatCompare(const CAddrReport& a, const CAddrReport& b) {
  if (a.uptime[4] == b.uptime[4]) {
    if (a.uptime[3] == b.uptime[3]) {
      return a.clientVersion > b.clientVersion;
    } else {
      return a.uptime[3] > b.uptime[3];
    }
  } else {
    return a.uptime[4] > b.uptime[4];
  }
}

extern "C" void* ThreadDumper(void* arg) {
  CChain* chain = (CChain*)arg;
  applyChain(chain);
  std::string datPath  = chain->datadir + "/dnsseed.dat";
  std::string datNew   = chain->datadir + "/dnsseed.dat.new";
  std::string dumpPath = chain->datadir + "/dnsseed.dump";
  std::string statPath = chain->datadir + "/dnsstats.log";
  int count = 0;
  do {
    Sleep(100000 << count); // First 100s, than 200s, 400s, 800s, 1600s, and then 3200s forever
    if (count < 5)
        count++;
    {
      vector<CAddrReport> v = chain->db.GetAll();
      sort(v.begin(), v.end(), StatCompare);
      FILE *f = fopen(datNew.c_str(),"w+");
      if (f) {
        {
          CAutoFile cf(f);
          cf << chain->db;
        }
        rename(datNew.c_str(), datPath.c_str());
      }
      FILE *d = fopen(dumpPath.c_str(), "w");
      fprintf(d, "# address                                        good  lastSuccess    %%(2h)   %%(8h)   %%(1d)   %%(7d)  %%(30d)  blocks      svcs  version\n");
      double stat[5]={0,0,0,0,0};
      for (vector<CAddrReport>::const_iterator it = v.begin(); it < v.end(); it++) {
        CAddrReport rep = *it;
        fprintf(d, "%-47s  %4d  %11" PRId64 "  %6.2f%% %6.2f%% %6.2f%% %6.2f%% %6.2f%%  %6i  %08" PRIx64 "  %5i \"%s\"\n", rep.ip.ToString().c_str(), (int)rep.fGood, rep.lastSuccess, 100.0*rep.uptime[0], 100.0*rep.uptime[1], 100.0*rep.uptime[2], 100.0*rep.uptime[3], 100.0*rep.uptime[4], rep.blocks, rep.services, rep.clientVersion, rep.clientSubVersion.c_str());
        stat[0] += rep.uptime[0];
        stat[1] += rep.uptime[1];
        stat[2] += rep.uptime[2];
        stat[3] += rep.uptime[3];
        stat[4] += rep.uptime[4];
      }
      fclose(d);
      FILE *ff = fopen(statPath.c_str(), "a");
      fprintf(ff, "%llu %g %g %g %g %g\n", (unsigned long long)(time(NULL)), stat[0], stat[1], stat[2], stat[3], stat[4]);
      fclose(ff);
    }
  } while(1);
  return nullptr;
}

extern "C" void* ThreadStats(void*) {
  bool first = true;
  do {
    char c[256];
    time_t tim = time(NULL);
    struct tm *tmp = localtime(&tim);
    strftime(c, 256, "[%y-%m-%d %H:%M:%S]", tmp);
    if (!first)
      printf("\x1b[%dA", (int)g_chains.size()); // move cursor up to the top of the block
    first = false;
    for (unsigned int ci=0; ci<g_chains.size(); ci++) {
      CChain* ch = g_chains[ci];
      CAddrDbStats stats;
      ch->db.GetStats(stats);
      uint64_t requests = 0;
      for (unsigned int i=0; i<ch->dnsThreads.size(); i++)
        requests += ch->dnsThreads[i]->dns_opt.nRequests;
      printf("\x1b[2K%s %-12s %i/%i available (%i tried in %is, %i new, %i active), %i banned; %llu DNS req\n",
             c, ch->name.c_str(), stats.nGood, stats.nAvail, stats.nTracked, stats.nAge, stats.nNew,
             stats.nAvail - stats.nTracked - stats.nNew, stats.nBanned, (unsigned long long)requests);
    }
    Sleep(1000);
  } while(1);
  return nullptr;
}

extern "C" void* ThreadSeeder(void* arg) {
  CChain* chain = (CChain*)arg;
  applyChain(chain);
  vector<string> vDnsSeeds;
  for (const string& seed: chain->seeds) {
    size_t len = seed.size();
    if (len > 6 && !seed.compare(len - 6, 6, ".onion")) {
      chain->db.Add(CService(seed.c_str(), GetDefaultPort()), true);
    } else {
      vDnsSeeds.push_back(seed);
    }
  }
  do {
    for (const string& seed: vDnsSeeds) {
      vector<CNetAddr> ips;
      LookupHost(seed.c_str(), ips);
      for (vector<CNetAddr>::iterator it = ips.begin(); it != ips.end(); it++) {
        chain->db.Add(CService(*it, GetDefaultPort()), true);
      }
    }
    Sleep(1800000);
  } while(1);
  return nullptr;
}

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

static bool parseMagic(const std::string& hex, unsigned char out[4]) {
  if (hex.size() != 8) return false;
  for (int i = 0; i < 4; i++) {
    unsigned int b = 0;
    if (sscanf(hex.c_str() + i*2, "%2x", &b) != 1) return false;
    out[i] = (unsigned char)(b & 0xff);
  }
  return true;
}

// Parse the multi-chain config: one [section] per chain, then key = value lines.
static bool LoadConfig(const CDnsSeedOpts& opts, std::vector<CChain*>& chains) {
  std::ifstream f(opts.config);
  if (!f.is_open()) { fprintf(stderr, "Cannot open config file '%s'\n", opts.config); return false; }
  std::string line;
  CChain* cur = NULL;
  while (std::getline(f, line)) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') continue;
    if (t[0] == '[' && t[t.size()-1] == ']') {
      cur = new CChain();
      cur->name = trim(t.substr(1, t.size()-2));
      cur->dnsPort = opts.nPort;
      cur->listen = opts.ip_addr ? opts.ip_addr : "::";
      cur->nThreads = opts.nThreads;
      cur->nDnsThreads = opts.nDnsThreads;
      cur->filter_whitelist = opts.filter_whitelist;
      cur->datadir = std::string(opts.datadir) + "/" + cur->name;
      chains.push_back(cur);
      continue;
    }
    size_t eq = t.find('=');
    if (eq == std::string::npos || !cur) continue;
    std::string key = trim(t.substr(0, eq));
    std::string val = trim(t.substr(eq + 1));
    if (key == "magic") { if (!parseMagic(val, cur->magic)) fprintf(stderr, "[%s] bad magic '%s' (need 8 hex chars)\n", cur->name.c_str(), val.c_str()); }
    else if (key == "port") cur->port = (unsigned short)atoi(val.c_str());
    else if (key == "minheight") cur->minHeight = atoi(val.c_str());
    else if (key == "minversion") cur->minVersion = atoi(val.c_str());
    else if (key == "seeds") { std::istringstream iss(val); std::string s; while (iss >> s) cur->seeds.push_back(s); }
    else if (key == "host") cur->host = val;
    else if (key == "ns") cur->ns = val;
    else if (key == "mbox") cur->mbox = val;
    else if (key == "listen") cur->listen = val;
    else if (key == "dnsport") cur->dnsPort = atoi(val.c_str());
    else if (key == "threads") cur->nThreads = atoi(val.c_str());
    else if (key == "dnsthreads") cur->nDnsThreads = atoi(val.c_str());
  }
  return true;
}

int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  setbuf(stdout, NULL);
  CDnsSeedOpts opts;
  opts.ParseCommandLine(argc, argv);
  printf("Supporting whitelisted filters: ");
  for (std::set<uint64_t>::const_iterator it = opts.filter_whitelist.begin(); it != opts.filter_whitelist.end(); it++) {
      if (it != opts.filter_whitelist.begin()) {
          printf(",");
      }
      printf("0x%lx", (unsigned long)*it);
  }
  printf("\n");
  if (opts.tor) {
    CService service(opts.tor, 9050);
    if (service.IsValid()) {
      printf("Using Tor proxy at %s\n", service.ToStringIPPort().c_str());
      SetProxy(NET_TOR, service);
    }
  }
  if (opts.ipv4_proxy) {
    CService service(opts.ipv4_proxy, 9050);
    if (service.IsValid()) {
      printf("Using IPv4 proxy at %s\n", service.ToStringIPPort().c_str());
      SetProxy(NET_IPV4, service);
    }
  }
  if (opts.ipv6_proxy) {
    CService service(opts.ipv6_proxy, 9050);
    if (service.IsValid()) {
      printf("Using IPv6 proxy at %s\n", service.ToStringIPPort().c_str());
      SetProxy(NET_IPV6, service);
    }
  }

  if (!LoadConfig(opts, g_chains)) exit(1);
  if (g_chains.empty()) { fprintf(stderr, "No chains defined in '%s'.\n", opts.config); exit(1); }

  mkdir(opts.datadir, 0755);
  pthread_attr_t attr_crawler;
  pthread_attr_init(&attr_crawler);
  pthread_attr_setstacksize(&attr_crawler, 0x20000);

  for (unsigned int ci = 0; ci < g_chains.size(); ci++) {
    CChain* ch = g_chains[ci];
    if (ch->port == 0) { fprintf(stderr, "[%s] missing 'port' in config\n", ch->name.c_str()); exit(1); }
    applyChain(ch);                       // so any IsGood() during .dat load uses this chain's params
    mkdir(ch->datadir.c_str(), 0755);

    std::string datPath = ch->datadir + "/dnsseed.dat";
    FILE *f = fopen(datPath.c_str(), "r");
    if (f) {
      CAutoFile cf(f);
      cf >> ch->db;
      if (opts.fWipeBan)    ch->db.banned.clear();
      if (opts.fWipeIgnore) ch->db.ResetIgnores();
    }

    bool dns = !ch->host.empty() && !ch->ns.empty() && !ch->mbox.empty();
    printf("[%s] magic=%02x%02x%02x%02x port=%u minheight=%i minversion=%i seeds=%i %s\n",
           ch->name.c_str(), ch->magic[0], ch->magic[1], ch->magic[2], ch->magic[3],
           ch->port, ch->minHeight, ch->minVersion, (int)ch->seeds.size(),
           dns ? "(DNS seed)" : "(crawler-only)");
    if (dns)
      printf("[%s]   serving %s via %s on %s port %i\n", ch->name.c_str(), ch->host.c_str(), ch->ns.c_str(), ch->listen.c_str(), ch->dnsPort);

    pthread_t t;
    if (dns) {
      for (int i = 0; i < ch->nDnsThreads; i++) {
        CDnsThread* dt = new CDnsThread(ch, i);
        ch->dnsThreads.push_back(dt);
        pthread_create(&t, NULL, ThreadDNS, dt);
        Sleep(20);
      }
    }
    pthread_create(&t, NULL, ThreadSeeder, ch);
    for (int i = 0; i < ch->nThreads; i++) {
      pthread_t ct;
      pthread_create(&ct, &attr_crawler, ThreadCrawler, ch);
    }
    pthread_create(&t, NULL, ThreadDumper, ch);
  }
  pthread_attr_destroy(&attr_crawler);

  printf("Started %i chain(s) in one process.\n", (int)g_chains.size());
  pthread_t threadStats;
  pthread_create(&threadStats, NULL, ThreadStats, NULL);
  pthread_join(threadStats, NULL);
  return 0;
}
