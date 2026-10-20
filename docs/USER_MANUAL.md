# NippApp user manual

NippApp version 0.1.0

NippApp is a headless DVB-NIP home gateway. It receives DVB-NIP carriers,
finds the services they carry, and serves them on the local network as a
DVB-I endpoint: service list entry points, service lists, DASH and HLS
manifests and segments, content guide data and files from object carousels.
Any DVB-I client can use it: the web player that comes with NippApp, a
DVB-I app on a set-top box or TV, or your own client.

It follows the home gateway model DM3 of the DVB-NIP specification (DVB
A180r3, draft ETSI TS 103 876). Clause numbers in this manual refer to that
specification. DVB-I behaviour follows ETSI TS 103 770.

Until tuner hardware is supported, NippApp receives from recordings: a
*sources file* maps each carrier to a capture file, which NippApp plays in
real time as if it were being received.

This manual is for engineers and testers who build, install and run
NippApp. It assumes you are familiar with Linux and with DVB. The design and
the interfaces to DVBCore are described in [ARCHITECTURE.md](ARCHITECTURE.md).

Contents:

1. [Requirements](#1-requirements)
2. [Getting NippApp](#2-getting-nippapp)
3. [The sources file](#3-the-sources-file)
4. [Running NippApp](#4-running-nippapp)
5. [The web player](#5-the-web-player)
6. [Other DVB-I clients](#6-other-dvb-i-clients)
7. [HTTP interface](#7-http-interface)
8. [Admin API](#8-admin-api)
9. [Network access](#9-network-access)
10. [Troubleshooting](#10-troubleshooting)
11. [Limitations](#11-limitations)
12. [Testing](#12-testing)

---

## 1. Requirements

- A 64-bit (x86-64) Linux PC. Ubuntu 24.04 or later, or Debian 12 or later,
  are the tested systems. WSL2 works, with the network limits given in
  [section 9](#9-network-access).
- For building: a C compiler and make, and the development packages for
  SQLite, libxml2, libjpeg, libpng, zlib and FreeType.
- For the mDNS announcement (optional): avahi-daemon running, and
  libavahi-client.
- For building the web player (once): Node.js 20.19 or later, access to the
  npm registry, and a checkout of `dvbi-client-js`.
- Captures of DVB-NIP carriers: MPEG transport stream files carrying MPE, or
  PCAP files of IP packets taken from a GSE carrier.

Memory: NippApp keeps received files in memory, up to 256 MB in all, unless
large files are sent to disk ([section 4.5](#45-the-disk-store)). Allow about
512 MB for the process, and disk space for the disk store if you use one.

## 2. Getting NippApp

There are two ways: build from source, or use a package built on another
machine.

### 2.1 Building from source

NippApp is built against DVBCore, which must be checked out beside it:

```
DTVkit/
  DVBCore/      branch dvbnip-gateway
  NippApp/
```

1. Install the build dependencies (Ubuntu or Debian):

   ```
   sudo apt install build-essential git libsqlite3-dev libxml2-dev libjpeg-dev \
       libpng-dev zlib1g-dev libfreetype-dev
   ```

   For the mDNS announcement, also:

   ```
   sudo apt install avahi-daemon avahi-utils libavahi-client-dev
   sudo systemctl enable --now avahi-daemon
   ```

2. Build DVBCore. Its environment script sets the options NippApp needs
   (DVB-NIP and SQLite integrated):

   ```
   cd DTVkit/DVBCore
   source ./example_setenv.sh
   make -j8
   ```

   This produces `DVBCore/build/bin/libdvbcore.a`.

3. Build NippApp:

   ```
   cd DTVkit/NippApp
   make
   ```

   This produces `build/nippapp`. Make options:

   | Command | Builds |
   |---|---|
   | `make` | the normal (debug) build |
   | `make DEBUG=0` | an optimised build |
   | `make ASAN=1` | a build with AddressSanitizer (run `make clean` when switching) |
   | `make MDNS=0` | a build without the mDNS announcement |

   If `libavahi-client-dev` isn't installed, NippApp is built without the
   mDNS announcement and says so when it starts.

4. Build the web player's libraries, once:

   ```
   tools/build-player.sh
   ```

   This builds `dvbi-client-js` from its checkout (by default
   `~/dvbi-client-js`; set `DVBI_CLIENT_JS` to use another) and fetches
   dash.js 4.7.4 and hls.js 1.5.20 into `player/vendor/`, which isn't kept
   in git. After that, the gateway needs no internet access to serve the
   player.

### 2.2 Using a package

A package runs on another PC without building there. On the build machine:

```
tools/package.sh            # or: tools/package.sh MDNS=0
```

This writes `build/nippapp-<version>-linux-x86_64.tar.gz`, which contains:

| File | Content |
|---|---|
| `bin/nippapp` | the optimised executable |
| `lib/` | the libraries whose versions differ between distributions: libxml2, ICU, SQLite, libjpeg, libpng |
| `player/` | the web player, with its libraries |
| `sources.conf` | the build machine's sources file, to edit |
| `README.txt` | what the target needs, and how to run it |

On the target:

```
tar xzf nippapp-0.1.0-linux-x86_64.tar.gz
cd nippapp-0.1.0
# copy the captures over and put their paths in sources.conf
bin/nippapp --scan
```

The target needs the build machine's glibc version or later. A package built
on Ubuntu 24.04 needs glibc 2.38, which Ubuntu 24.04 and later have. To
support older systems, build the package on the oldest one. Avahi, D-Bus,
zlib and the C++ runtime come from the target, as every desktop install has
them. On a server install, add `sudo apt install libavahi-client3
avahi-daemon`, or use a package built with `MDNS=0`.

## 3. The sources file

The sources file says which carriers NippApp can "receive", and which
capture stands in for each. NippApp reads `sources.conf` in the current
directory unless `-s` names another.

Each line is:

```
freq(MHz)  pol  symrate(ksym/s)  kind  file  [loop|stop]
```

| Field | Meaning |
|---|---|
| `freq` | the carrier's RF frequency in MHz |
| `pol` | `H` or `V` |
| `symrate` | symbol rate in ksym/s |
| `kind` | `ts`: a transport stream carrying MPE; `pcap`: IP packets from a GSE carrier |
| `file` | the capture; a relative path is relative to the sources file |
| `loop` or `stop` | at the end of the file, start again, or stop (the default) |

Lines starting with `#` are comments. Example:

```
# freq(MHz) pol  symrate  kind  file                                      end
11471       V    27500    ts    ../Streams/DvbNIP/1097000.ts              stop
11141       H    23500    pcap  ../Streams/DvbNIP/2025-02-11_1min.pcap    loop
```

Rules:

- **Use the carrier's real tuning parameters, as its NIF gives them.** A
  search tunes what the sources file says, but when a client asks for a
  service, NippApp tunes what the broadcast's NIF says. A tune matches a
  source within 4 MHz and 2% of the symbol rate. A tune that matches no
  source gets no lock, as an empty transponder would.
- **Loop short captures.** When a source stops, the tuner stays locked but
  receives nothing, so later requests on that carrier can't be answered. A
  one-minute capture should loop. Looping takes the broadcast clock back by
  the capture's length, which players may stumble over at that moment.
- **Use one capture per carrier,** and preferably captures with the same
  clock. Each recording has its own time, and NippApp keeps one broadcast
  clock: while two carriers from different recordings are tuned, the clock
  follows whichever spoke last, and a player on the other may ask for
  segments that never come.
- **TS captures** are fed in real time, paced by their PCR, or by the TDT/TOT
  if they have no PCR.
- **PCAP captures** are classic PCAP (not pcapng), with Ethernet (with or
  without a VLAN tag), raw IP or Linux cooked frames. They are paced by their
  capture timestamps.

## 4. Running NippApp

### 4.1 First run

```
cd DTVkit/NippApp
./build/nippapp -d ~/nippapp-data -s sources.conf
```

On the first run (an empty database), NippApp searches every source in turn,
then starts the HTTP server:

```
NippApp 0.1.0 starting
NippApp: 2 sources in /home/user/DTVkit/NippApp/sources.conf
NippApp: DVBCore initialised, 0 services in the database
NippApp: pre-buffering every channel on the carrier being received
NippApp: first run, creating the satellite and LNB records
NippApp: searching 11471 MHz V 27500 ksym/s (MPE)
...
NippApp: HTTP server on port 8080, player at http://localhost:8080/player/
NippApp: mDNS: _dvbservdsc._tcp on eth0, dvbi_sep=http://192.168.1.20:8080/dvbi/slep.xml
```

A search takes from a few seconds to about 20 s per carrier (a carrier
without DVB-I entry points or list waits for a 20 s timeout). Each search is
limited to 180 s. The channels found are printed with their LCNs.

Later runs use the database and start serving at once. A carrier is tuned
only when a client asks for something on it.

### 4.2 Command-line options

| Option | Meaning | Default |
|---|---|---|
| `-d`, `--dir <path>` | working directory: the database, the received signalling and the registry are kept here | the current directory |
| `-s`, `--sources <file>` | the sources file | `sources.conf` |
| `-S`, `--scan` | search every source, even if the database has services | search only on the first run |
| `-c`, `--country <code>` | ISO 3166 country (3 letters), which decides how LCNs are allocated | `gbr` |
| `-p`, `--port <port>` | HTTP port | `8080` |
| `-P`, `--player-dir <dir>` | the web player's files | `player/` next to the executable's directory |
| `-A`, `--admin-from-any` | answer the admin API from any address, not only this machine | off |
| `-n`, `--name <name>` | the name announced with mDNS | `NippApp on <hostname>` |
| `-M`, `--no-mdns` | don't announce the gateway with mDNS | announced |
| `-b`, `--prebuffer <n>` | pre-buffering: `all`, `off`, or the most channels per carrier (see [section 5.3](#53-channel-changes-and-pre-buffering)) | `all` |
| `-D`, `--store-dir <dir>` | receive large files to disk in this directory, and serve them from there ([section 4.5](#45-the-disk-store)) | every file in memory |
| `-T`, `--disk-threshold <size>` | files this large or larger go to disk | `4M` |
| `-L`, `--disk-limit <size>` | the most the disk store holds, at most `4095M` | `2G` |
| `-a`, `--disk-all` | every service's files of the threshold or more go to disk, live segments included, not only object carousels' | off |
| `-h`, `--help` | show the options | |
| `-V`, `--version` | show the version | |

Search again after changing the sources file, or to pick up a broadcast's
new services: run with `--scan`, or ask the running gateway with
`POST /api/scan` ([section 8](#8-admin-api)). A change to a broadcast's
signalling (a new SIF or service list) is also picked up while a carrier is
tuned, without a search.

### 4.3 The working directory

NippApp keeps its state in the working directory (`-d`):

| Path | Content |
|---|---|
| `dtvkit.sqlite3` | DVBCore's channel database: transports, services, names, LCNs |
| `nvm.dat` | DVBCore's settings, e.g. the country |
| `disks/nip/` | the NIP signalling: `nif.xml`, the SIFs (`sif.xml`, `sif1.xml`, ...), the DVB-I lists (`dvbisl*.xml` with their locations in `.loc`) |
| `registry/` | every DVB-I document received (entry points, service lists, playlists), at its broadcast path, with its media type in a `.nippapp-type` file beside it |
| `registry/slep/` | each network's service list entry points, named after the registry entity (e.g. `SES_DVB-NIP.xml`) |
| `registry/urn/` | documents the broadcast identifies by URN |
| `disks/nip/store` | with `--store-dir`, a link to the disk store's directory |

To start again from nothing, stop NippApp and delete the working directory.
To keep the channels but search again, run with `--scan`.

### 4.4 Stopping

Press Ctrl-C, or send SIGTERM or SIGQUIT. NippApp withdraws its mDNS
announcement, stops the HTTP server, saves the database and exits with code
0:

```
NippApp: stopped
```

### 4.5 The disk store

By default every received file is kept in memory. Large files from object
carousels, such as SES's offline videos and PDFs (17-291 MB), don't fit, so
they need a disk store:

```
./build/nippapp -d ~/nippapp-data --store-dir /data/nippapp-store
```

```
NippApp: disk store in /data/nippapp-store: carousels' files of 4096 KB or more, up to 2048 MB
```

- Carousel files of 4 MB or more (`--disk-threshold`) are received to disk
  and served from there. Smaller files and live segments stay in memory.
- A file that misses packets in one pass of its carousel is completed by
  later passes, as long as it is still being asked for (the player asks
  every 5 s while it waits). Once complete, it isn't received again when the
  carousel comes round, unless it changes.
- When the store reaches `--disk-limit` (default 2 GB), the oldest complete
  files are dropped. A file larger than the limit isn't received.
- Files aren't kept across restarts: NippApp empties the directory of its
  own files (`nip-1`, `nip-2`, ...) when it starts. Other files in the
  directory are left alone.
- `--disk-all` sends every service's files of the threshold or more to disk,
  live segments included. With `--disk-all --disk-threshold 0`, everything
  except the signalling goes to disk. This is for a box with little memory;
  on flash storage, it means many writes.

`/api/status` shows the store's use under `diskStore`
([section 8.1](#81-get-apistatus)).

## 5. The web player

The web player is a DVB-I client served by NippApp. It finds services only
through the gateway's DVB-I endpoints, as any other client would. It uses
dvbi-client-js for DVB-I, dash.js for DASH and hls.js for HLS.

### 5.1 Opening it

Open `http://<gateway address>:8080/player/` in a current desktop browser
with Media Source Extensions (needed by dash.js and hls.js). On the gateway itself, `http://localhost:8080/player/`.
From another machine, use the gateway's IP address (see
[section 6.3](#63-addresses)).

The player reads the entry points at `/dvbi/slep.xml` and offers the service
lists they name in the list selector, as *provider: list name*. It loads the
first list and shows its services in LCN order, with their logos.

Two optional parameters choose where to start:

| Address | Starts from |
|---|---|
| `/player/?list=/ses.com/dvbi/service_list_full.xml` | that service list only, without the entry points |
| `/player/?slep=/dvbi/slep.xml?TargetCountry=GBR` | other entry points, e.g. a filtered query |

The value can be a path on the gateway or a full URL.

### 5.2 Playing a service

Each service shows a button for each way it can be played:

| Button | Plays |
|---|---|
| **DASH** | the service's DASH instance, with dash.js |
| **HLS** | its HLS instance, with hls.js, when the list signals one |
| **DVB-HB** | a TS service for a DVB-HB gateway to re-package (Annex F). NippApp can't do this and answers 501, which the player reports. |
| **Picture**, **Video**, **PDF** | a file from an object carousel (offline content, [section 5.4](#54-offline-content)) |

The log panel under the video shows what the player is doing:

```
Requesting http://192.168.1.20:8080/ses.com/livesim2/B1/Manifest.mpd
Joined; waiting 7.0 s for the first segments
Playing http://192.168.1.20:8080/ses.com/livesim2/B1/Manifest.mpd with dash.js
Stream initialised
Playing
```

The first service on a carrier takes a few seconds: the first request tunes
the carrier, NippApp waits for the manifest, and the player waits for the
first segments after the join. Only segments broadcast after the join are
received, and the player starts 4 s behind the live edge.

If the gateway can't serve the service yet, the player shows the reason and
tries again after the time the gateway gives (503 with `Retry-After`):

| Message | Meaning |
|---|---|
| `Gateway: no tuner free` | every tuner is in use for other carriers; one is freed 20-40 s after its services stop being requested |
| `Gateway: the carrier can't be received (no signal)` | the carrier didn't lock: no signal, or no capture for it in the sources file |

### 5.3 Channel changes and pre-buffering

While a client receives a channel, NippApp also receives the other channels
on the same carrier, in the same format. This is *pre-buffering*. The carrier
is already tuned, so no other tuner is used. A change to another channel on
the carrier then finds its latest segments already at the gateway, and the
player starts it at once:

```
Received for 15 s already: starting at once
```

A channel on another carrier, or one that has just been tuned, starts after
the usual wait.

Pre-buffered channels stop when no client receives any channel on the
carrier, and the tuner is then released as usual. They keep only their last
12 s of files, so a carrier with many channels doesn't fill the gateway's
memory. `--prebuffer 4` limits pre-buffering to four channels per carrier,
in the order the SIF lists them; `--prebuffer off` turns it off.

### 5.4 Offline content

Some lists offer files from the broadcast's object carousels, such as SES's
"DVB-NIP Offline Content" list of pictures, videos and PDFs. Choosing one
requests the file until it arrives: a carousel can take a minute or more to
come round, so the player tries again every 5 s for up to three minutes. A
picture is shown in place of the video, a video is played as a file, and a
PDF is shown in a frame.

Files that are only partly in a capture never arrive. Large files (videos,
PDFs) need a disk store ([section 4.5](#45-the-disk-store)); a video from it
can be seeked, as the gateway answers byte-range requests.

### 5.5 Content guide

The player doesn't show a guide yet. A DVB-I client can ask the gateway for
schedule and now/next data ([section 7.3](#73-content-guide-queries)).

## 6. Other DVB-I clients

### 6.1 What to configure

A DVB-I client that is set up by hand needs one of:

- **The service list entry points:** `http://<gateway address>:8080/dvbi/slep.xml`.
  This lists every network's service lists that the gateway has received.
- **A single service list,** at its broadcast path. Which lists exist
  depends on the broadcasts; read them from the entry points:

  ```
  curl -s http://<gateway address>:8080/dvbi/slep.xml | grep -o '<URI>[^<]*'
  ```

  With the test recordings these include
  `/ses.com/dvbi/service_list_full.xml`, `/dvb_i_sl_physical.xml` and
  `/main_service_list.xml`.

When a broadcast has services that no broadcast list covers, NippApp offers
its own list, `/dvbi/nippapp_list.xml` ("Services without a broadcast
list"), in the entry points too.

### 6.2 Discovery with mDNS

NippApp announces itself with DNS-SD over mDNS on each IPv4 interface
(clause 11.1), as two services named `NippApp on <hostname>`:

| Service type | TXT record |
|---|---|
| `_dvbservdsc._tcp` | `txtvers=1`, `dvbi_sep=http://<address>:<port>/dvbi/slep.xml`, `manuf=DTVKit`, `model=NippApp`, `tuners=DVB-S2/<number of tuners>` |
| `_http._tcp` | `path=/` |

Check it from another Linux machine:

```
avahi-browse -rt _dvbservdsc._tcp
```

If another gateway already has the name, Avahi renames this one
(`NippApp on host #2`). Use `--name` to choose a name and `--no-mdns` to
turn the announcement off. The announcement needs avahi-daemon; without it,
NippApp waits for the daemon and announces itself when the daemon starts.

Browser-based clients can't use mDNS, so they are given the address by hand,
or load the player from the gateway.

### 6.3 Addresses

Always reach the gateway by its IP address. The broadcast's documents name
the gateway `http://dvb.gw/`. NippApp replaces that with the address the
client used in its request, so every link in a list or manifest points back
to the same address. dvbi-client-js allows plain HTTP only to private IP
addresses and localhost, so a `.local` name doesn't work with it.

NippApp serves plain HTTP only.

### 6.4 Clocks

Live services are timed by the broadcast clock, which NippApp serves at
`/time` and names in every DASH manifest (`UTCTiming`). A client that uses it
plays the recordings correctly. A client that shows "now and next" from its
own clock finds no current programmes in the recordings, as their guide data
is from the time they were recorded.

## 7. HTTP interface

NippApp answers on one port (default 8080). Every response allows any origin
(`Access-Control-Allow-Origin: *`), as DVB-I clients are often web pages
served from elsewhere. Only GET and HEAD are accepted, except for the admin
API.

### 7.1 Paths

| Path | Content |
|---|---|
| `/` | an index page |
| `/player/` (and `/player`) | the web player |
| `/time` | the broadcast wall clock (clause 8.6.3), as xs:dateTime in UTC with milliseconds (`2025-02-11T14:21:01.250Z`); `?iso` or `?xsdate` give whole seconds. 503 until a broadcast time has been received. |
| `/dvbi/slep.xml` | the service list entry points of every network received, merged into one document (clause 8.1.7.3) |
| `/dvbi/nippapp_list.xml` | NippApp's own list of services that no broadcast list covers, when there are any |
| any broadcast path | the file as broadcast at `http://dvb.gw/<path>`: entry points, service lists, manifests, playlists, segments, pictures, guide documents |
| `/api/status`, `/api/scan` | the admin API ([section 8](#8-admin-api)) |

What NippApp changes in what it serves:

- In DVB-I documents (entry points and service lists), `http://dvb.gw/` is
  replaced by the gateway's address.
- In DASH manifests, `UTCTiming` points at `/time`, and live manifests get
  `minimumUpdatePeriod` 2 s.
- Files sent compressed (gzip, zlib, deflate) are served decoded.
- Large files, files on disk and requests with a `Range` header are sent
  as they are read, with `Accept-Ranges: bytes`. One byte range is
  answered with 206 and `Content-Range`; several ranges in one request get
  the whole file.
- Manifests and playlists of a service being received carry
  `X-NippApp-Receiving: <seconds>`, how long the gateway has been receiving
  that service. The web player uses it to start a pre-buffered channel at
  once; other clients can ignore it.

### 7.2 Entry point queries

The entry points can be filtered with the query parameters of the DVB-I
registry interface (TS 103 770 clause 5.1.3.2), on `/dvbi/slep.xml` or on a
broadcast entry points file:

| Parameter | Matches |
|---|---|
| `TargetCountry` | an offering's target countries (3-letter codes) |
| `Language` | its languages (`en` matches `en-GB` and the other way round) |
| `Genre` | its genre hrefs |
| `ProviderName` | the provider's name, ignoring case |
| `Delivery` | `dvb-dash`, `dvb-t`, `dvb-c`, `dvb-s`, `dvb-iptv` or `application` |
| `regulatorListFlag` | `true` for regulator lists; `false` applies no filter |
| `inlineImages` | accepted; images are still given as URLs |

Each parameter can be given as `Name` or `Name[]`, and repeated. A list
matches if it matches any value of every parameter given. An offering that
doesn't declare a property matches whatever is asked for it. An undefined
parameter, or an invalid value, gets 400. Example:

```
curl 'http://localhost:8080/dvbi/slep.xml?ProviderName=SES&Delivery=dvb-dash'
```

### 7.3 Content guide queries

A service list's `ScheduleInfoEndpoint` for a broadcast list is the
broadcast's Content Guide Manifest, for example
`/ses.com/dvbi/cg/manifest.xml`. NippApp answers DVB-I schedule queries on it
(TS 103 770 clause 6.5), using the guide documents from the broadcast's
carousels:

| Query | Returns |
|---|---|
| `?sid=<ServiceIDRef>&start=<s>&end=<s>` | events starting in `[start, end)`; `start` and `end` in seconds since 1970, on whole 3 hours, 6 or 12 hours apart, within 28 days |
| `...&inclusive=true` | also the event under way at `start` |
| `?sid=<ServiceIDRef>&now_next=true` | the event on now (by the broadcast clock) and the next one |
| `?sid=<ServiceIDRef>&now_next=window` | the event on now, with up to ten before and ten after |

The answer is a TV-Anytime `TVAMain` document. A query that breaks the rules
gets 400. An unknown service gets empty tables. While the guide is still
being received, the answer is 503 with `Retry-After`.

### 7.4 Status codes

| Code | When |
|---|---|
| 200 | the file or answer |
| 206 | the byte range asked for |
| 302 | `/player` redirects to `/player/` |
| 400 | an entry point or guide query that isn't valid |
| 403 | the admin API, asked from another machine without `--admin-from-any` |
| 404 | a path no broadcast signals (answered at once), or a signalled file that hasn't arrived within 8 s |
| 405 | a method other than GET or HEAD |
| 409 | `POST /api/scan` while a scan is running |
| 416 | a byte range past the end of the file |
| 501 | a request with `src=DVB-HB` in its query: DVB-HB isn't supported |
| 503 | not available yet; `Retry-After` says when to try again, and the body says why: `no tuner free` (10 s), `the carrier can't be received (no signal)` (30 s), `waiting for the broadcast carousel` (10 s), `receiving the content guide` |

## 8. Admin API

The admin API answers only on the gateway itself (127.0.0.1 or ::1), unless
NippApp was started with `--admin-from-any`.

### 8.1 GET /api/status

Returns JSON describing the gateway:

```
curl -s http://localhost:8080/api/status | python3 -m json.tool
```

| Field | Content |
|---|---|
| `version`, `uptimeSeconds` | NippApp's version, and how long it has run |
| `scan` | `running`; `lastCompleted` and `lastFinished` after a scan |
| `broadcastTime` | the broadcast clock, as `/time` gives it |
| `sources` | the sources file's entries |
| `tuners` | each tuner path: `locked`, and the source it plays (`null` if none) |
| `prebuffer` | `enabled`, and `maxPerCarrier` (`null` for every channel) |
| `streams` | each NIP stream (carrier) the signalling names: `network`, `carrier`, `service`, `tuned`, `noTuner`, `noSignal`, `entries` (SIF entries on it), `prebuffered` (how many are) |
| `streams[].receiving` | each service being received on it: `path`, `clients`, `idleSeconds` since its files were last requested, `bytes` held, `prebuffered` |
| `fileStore` | the memory used for received files: `bytes`, `files`, `maxBytes` |
| `diskStore` | the disk store: `enabled`, `bytes` (complete files and files being received), `files` (complete), `maxBytes`, `threshold`, `allServices` |
| `channels` | channels in the database |
| `networks` | the networks whose entry points are kept |

Extract of a status with SES's B1 playing:

```json
"streams": [
  {
    "network": 1, "carrier": 1045, "service": 0,
    "tuned": true, "noTuner": false, "noSignal": false,
    "entries": 53, "prebuffered": 2,
    "receiving": [
      { "path": "/ses.com/livesim2/B1/Manifest.mpd", "clients": 0, "idleSeconds": 3, "bytes": 1731117, "prebuffered": false },
      { "path": "/ses.com/livesim2/B2/Manifest.mpd", "clients": 0, "idleSeconds": 5, "bytes": 1821701, "prebuffered": true },
      { "path": "/ses.com/livesim2/C1/Manifest.mpd", "clients": 0, "idleSeconds": 5, "bytes": 985932, "prebuffered": true }
    ]
  }
]
```

### 8.2 POST /api/scan

Starts a search of every source while NippApp keeps serving:

```
curl -X POST http://localhost:8080/api/scan
```

| Code | Meaning |
|---|---|
| 202 | the scan has started; follow it in `/api/status` (`scan.running`) |
| 409 | a scan is already running |

## 9. Network access

To serve clients on other machines (an STB, a TV, another PC), the
gateway's HTTP port must be reachable from the LAN, and mDNS needs UDP port
5353.

**On a Linux PC** with `ufw` enabled:

```
sudo ufw allow 8080/tcp
sudo ufw allow 5353/udp
```

**Under WSL2** with its default NAT networking, the gateway can't be reached
from the LAN, and its mDNS announcement stays inside the WSL virtual
machine. Either:

- turn on WSL's mirrored networking (`networkingMode=mirrored` in
  `%UserProfile%\.wslconfig`, then `wsl --shutdown`); or
- forward the port from Windows (in an administrator prompt), and allow it
  in the Windows firewall:

  ```
  netsh interface portproxy add v4tov4 listenport=8080 listenaddress=0.0.0.0 connectport=8080 connectaddress=<WSL address>
  ```

  A port forward doesn't carry mDNS, so clients are given the Windows PC's
  address by hand.

Running NippApp on a native Linux PC on the LAN avoids these limits. Build it
there ([section 2.1](#21-building-from-source)) or copy a package
([section 2.2](#22-using-a-package)), copy the captures, and put their paths
in the sources file.

## 10. Troubleshooting

| Symptom | Likely cause | What to do |
|---|---|---|
| `error while loading shared libraries: libxml2.so.2` on another PC | the executable was built for another distribution's libraries | use a package ([section 2.2](#22-using-a-package)) or build on that PC |
| The player page says dash.js or hls.js is not loaded | `player/vendor/` hasn't been built | run `tools/build-player.sh` |
| The player can't be reached from another machine | firewall, or WSL NAT networking | see [section 9](#9-network-access) |
| No lists in the player, or none after a restart | no search yet, or the search found no entry points | run with `--scan`; check the search log |
| A service answers 404 after 8 s | the source for its carrier has stopped (end of file with `stop`), or the file isn't in the capture | use `loop` for short captures |
| `Gateway: the carrier can't be received (no signal)` | no source in the sources file for the carrier the NIF gives, or wrong tuning parameters | add the capture with the NIF's frequency, polarisation and symbol rate |
| `Gateway: no tuner free` | every tuner is in use for other carriers | wait: a tuner is freed 20-40 s after its services' last request |
| The player stops at "Stream initialised" | two carriers from recordings with different clocks are tuned | use one capture at a time, or captures with the same clock |
| A playing channel jumps back about a minute | a looping capture restarted | expected with `loop` |
| A picture never arrives | it is only partly in the capture | expected with short captures |
| `mDNS: waiting for avahi-daemon` | avahi-daemon isn't running | `sudo systemctl enable --now avahi-daemon`, or `--no-mdns` |
| `403` from `/api/status` | asked from another machine | run NippApp with `--admin-from-any` |
| `cannot start the HTTP server on port 8080; is it in use?` | another program, or another NippApp, has the port | use `-p` |

NippApp logs to standard output. Run it with `| tee nippapp.log` to keep a
copy.

## 11. Limitations

- **Inputs are recordings.** There is no support for tuner hardware yet. The
  file tuner offers two tuner paths, so two carriers can be received at
  once.
- **Memory.** Without a disk store, received files are kept in memory: up to
  256 MB in all and 128 MB per service, with no option to change these.
  Live segments are kept for 30 s (12 s when pre-buffered); carousel files
  are kept until replaced or over the limits.
- **Sizes.** A file, and the disk store, are under 4 GB.
- **DVB-NIP features not supported by DVBCore yet:** ROUTE (only FLUTE is
  received), compressed signalling files, 48-bit TOIs, and for GSE, ROHC and
  GSE-Lite link-layer signalling.
- **DVB-HB** isn't supported: requests with `src=DVB-HB` get 501.
- **Plain HTTP only;** no HTTPS.
- **One broadcast clock.** With recordings from different times, see
  [section 3](#3-the-sources-file).
- **LCNs.** DVBCore's database has one LCN space, so when two networks'
  lists ask for the same LCNs, one gets other numbers in the database. The
  lists NippApp serves keep each broadcast's own LCNs, so clients aren't
  affected.

## 12. Testing

`tools/asan-check.sh` builds NippApp with AddressSanitizer, runs it through
test scenarios on the captures, checks the results, and checks that each run
ends without memory errors or leaks. It then rebuilds the normal executable.
It takes about 15 minutes, as captures are fed in real time.

```
tools/asan-check.sh               # every scenario
tools/asan-check.sh prebuffer     # one scenario
KEEP=1 tools/asan-check.sh        # keep the logs and working directories
```

| Scenario | Checks |
|---|---|
| `scans` | each capture searched on its own finds its channels |
| `networks` | three networks on two tuners: names, merged entry points, entry point queries, 503 with both tuners busy, 501 for DVB-HB, fast 404, mDNS, admin API, a restart without a search |
| `change` | a broadcast's signalling changing between two captures: old paths gone, new ones playing, the database updated |
| `carousels` | SES's object carousels: the offline content list, an encoded file name, the decoded schedule, guide queries |
| `nolist` | a broadcast without DVB-I lists: NippApp's own list |
| `retune` | an HLS playlist after a re-tune is fresh |
| `prebuffer` | pre-buffering, channel changes, the no-signal 503, the DASH and HLS services sharing a directory |
| `disk` | the disk store, with a test capture made by `tools/make-carousel-pcap.py`: a 30 MB carousel file arriving whole, kept on disk, byte ranges, no second reception, the store emptied at startup, the disk limit |

Variables: `CAPTURES` (default `../Streams/DvbNIP` beside NippApp) is where
the captures are, and `PORT` (default 18080) is the port used.

`tools/make-carousel-pcap.py` makes a test capture with a large file complete
in an object carousel, since no recording has one. By default it takes SES's
capture and replaces its video carousel's session with generated packets
carrying a 30 MB file at the path of SES's `HDplus_ExtraScreen.mp4`:

```
tools/make-carousel-pcap.py ../Streams/DvbNIP/2025-02-11_1min.pcap /tmp/ses-carousel.pcap
```

`--file` sends a file of your own (e.g. a real MP4, to watch and seek in the
player), `--size` sets the generated file's size, and `--content-out` writes
the file sent, to compare with what the gateway serves. Use the new capture
in place of SES's in the sources file, with `loop`.
