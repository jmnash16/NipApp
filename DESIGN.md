# NippApp design outline

How to build, run and use NippApp is in [docs/USER_MANUAL.md](docs/USER_MANUAL.md);
its architecture and its interfaces with DVBCore are in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). This file records the design
decisions and the findings behind them, as development went.

NippApp is a headless DVB-NIP gateway built on DVBCore. It searches DVB-NIP
carriers (MPE and GSE), stores what it finds in DVBCore's channel database,
acts as a local DVB-I service list registry for the broadcast lists, tunes the
right carrier when a client asks for something, and serves manifests, segments,
service lists and guide data over HTTP.

It follows the DVB-NIP specification's home gateway model, DM3 (A180r3, draft
ETSI TS 103 876 V1.1.1, in `DVBCore/documents/`): a gateway that serves
unmodified DVB-I clients on the local network. Clause numbers below refer to
that document.

- **Phase A:** NippApp plus a new web player. The player is a DVB-I client
  from day one, built on `dvbi-client-js`: it learns about services only
  through the gateway's DVB-I endpoints, never from a private API.
- **Phase B:** NippApp as a full DVB-I endpoint that any DVB-I client can use:
  entry point queries, mDNS discovery, content guide queries, conformance.

The first scope is an MPE-only gateway (Annex A), which the specification
exempts from ROHC and GSE-Lite link-layer signalling. GSE is exercised from
PCAP files without those.

The test tool in `DVBCore/dvbnip/test` stays where it is, as a lightweight
harness for the library.

## Inputs

There is no tuner hardware yet, so the first inputs are files:

| Input | Becomes | DVBCore path exercised |
|---|---|---|
| TS file (e.g. `1097000.ts`) | a satellite TS transport | tuner → demux → MPE section filter → `DVBNIP_InjectIP` |
| PCAP file (e.g. SES `2025-02-11_1min.pcap`) | a satellite **GSE** transport (`use_gse`) | tuner → GSE data callback → `GSE_DataIn` → `DVBNIP_InjectIP` |

A PCAP holds IP packets that were already taken out of a GSE carrier. The
PCAP input therefore wraps each IP packet in one GSE packet (start and end
set, no label, protocol type 0x0800 or 0x86DD) and passes it to the callback
DVBCore registers with `STB_TuneSetGSEDataCallback`. That runs the real GSE
search (`APP_SI_MODE_GSE_NO_SI`, `ManageDVBNIPGSE`). Classic PCAP files are
read (not pcapng), with micro- or nanosecond timestamps in either byte order,
and Ethernet (with or without a VLAN tag), raw IP or Linux cooked frames. IP
packets are paced by their capture timestamps. A mutex held across the
callback lets DVBCore clear it and free its GSE context safely.

A sources file (`-s`, default `sources.conf`) maps carriers to files. Each
line is `freq(MHz) pol symrate(ksym/s) kind file [loop|stop]`. Relative file
names are relative to the sources file. The captures live in
`DTVkit/Streams/DvbNIP`.

```
# freq(MHz) pol  symrate  kind  file                                  end
11471       V    27500    ts    ../Streams/DvbNIP/1097000.ts          stop
11013       H    27500    ts    ../Streams/DvbNIP/1087000.ts          stop
11141       H    23500    pcap  ../Streams/DvbNIP/2025-02-11_1min.pcap stop
```

A source must have the carrier's real tuning parameters, as its NIF gives
them: a search tunes what the sources file says, but on-demand tuning tunes
what the NIF says. The SES capture is carrier 1045 at 11141 MHz H 23.5 Msym/s;
TP37's carrier is at 11023 MHz H 23.5 Msym/s.

DVBCore tunes a universal LNB. The file tuner therefore works out the RF
frequency from the IF plus the local oscillator that the 22 kHz tone selects
(9750 or 10600 MHz), and the polarisation from the LNB voltage. It matches a
source within 4 MHz and 2% of the symbol rate. DVBCore passes the LO in kHz
through a 16-bit parameter (`STB_TuneSetLOFrequency`), so that value cannot
be used. Tuning anything that matches no source reports no lock, as an empty
transponder would.

A TS source is fed in real time. It is paced by the first PCR PID seen. When
the capture has no PCR (the Inverto captures), it is paced by the TDT/TOT and
the average bitrate between them, as the test tool's `-r` does. At the end of
the file the source loops or stops; a stopped source leaves the tuner locked
with no data.

Every stream carries NIP Actual Carrier Information (TSI 1 on the announcement
address, LCT extension 68, every 250 ms; 8.7). For PCAP input, which is the
only record of the carrier's identity, the tuner logs it the first time it
appears after a tune. The SES capture is network 1, carrier 1045, link 1,
provider "SES"; TP37 carries none.

## Structure

```
NippApp/
  Makefile               builds against DVBCore's libdvbcore.a
  DESIGN.md
  sources.conf           example sources file
  inc/
    nippapp.h            interfaces between NippApp's own modules
  src/
    main.c               options, startup, main loop (runs scans asked for), shutdown
    admin.c              /api/status JSON
    events.c             DVBCore event handler (logging for now)
    search.c             first-run setup (country, satellite/LNB records) and scans
    registry.c           keeps every DVB-I document received on disk (query filtering: Phase B)
    guide.c              content guide endpoint (Annex D.6.2)
    httpd.c              HTTP server: broadcast files, DVB-I rewriting, /time, /dvbi/slep.xml (milestone 4)
    mdns.c               DNS-SD/mDNS announcement through avahi-daemon
  platform/
    COPIED.md            files copied from App, with their source and changes
    os/                  copy of App/platform/os/linux (tasks, mutexes, queues, clock)
    hw/
      inc/nipphw.h       sources and the tuner-to-demux link (not DVBCore's platform API)
      inc/stbhwdef.h     platform sizes, copied from App (2 tuner paths)
      src/stbhwtun.c     file tuner: sources file, lock, TS feeder, PCAP→GSE feeder
      src/stbhwdmx.c     section filtering, written for NippApp (no PES or decoding)
      src/stbhwmem.c, stbhwdsk.c, stbhwnet.c, stbhwcrypt.c, celo.c, stbhwsp.c,
          stbhwplatform.c, stbhwini.c
                         taken from App where they have no UI dependencies
      src/stubs.c        empty AV, OSD, front panel, PVR, VBI and EBU functions
  player/
    player.html          new web player (dvbi-client-js for metadata; dash.js + hls.js)
    vendor/              made by tools/build-player.sh (not in git): dvbi-client-js, dash.js, hls.js
  tools/
    build-player.sh      builds player/vendor/
  third_party/
    civetweb/            civetweb 1.16 (MIT), built without TLS, CGI and file serving
```

NippApp keeps its own copy of the platform layer rather than building from
`App/platform`. That way its repo depends only on DVBCore, and App keeps its
UI-specific simulator (`stbhwsimulator.c`: Lua, key events, `./ts/<freq>.ts`).
The cost is that fixes to the shared files have to be copied across by hand,
so the copied files are listed in `platform/COPIED.md` with their source.

The skeleton (milestone 1) runs with `./build/nippapp -d <dir>`. DVBCore keeps
`dtvkit.sqlite3` and `nvm.dat` in that directory. SIGINT, SIGTERM and SIGQUIT
save the database and exit with code 0.

About 400 platform functions exist. Only tuner, demux, OS, memory, storage and
network need real code; the rest are stubs, added as the linker asks for them.

## Build

1. DVBCore is built as usual with `DTVKIT_INTEGRATE_DVBNIP=1`,
   `DTVKIT_INTEGRATE_SQLITE=1` and a 64-bit environment.
2. NippApp's Makefile builds its own sources plus the platform layer, then
   links with `libdvbcore.a`, `sqlite3`, `xml2`, `jpeg`, `png`, `z` and
   `pthread` (DVBCore's image decoding needs jpeg and png). No SDL, ffmpeg,
   Lua or Qt. `make ASAN=1` builds with AddressSanitizer; run `make clean`
   when switching between the two. With `libavahi-client-dev` installed it
   also links libavahi-client for the mDNS announcement (`make MDNS=0` to
   leave it out); without it NippApp builds without the announcement.
3. The build defines `DVBNIP_BUILD`, the same as DVBCore.

## Running on a separate Linux PC

Under WSL2's default NAT networking the gateway can't be reached from the LAN
(an STB, another PC) and its mDNS announcement stays inside the WSL VM. A
64-bit Linux PC on the LAN avoids both. On Ubuntu or Debian:

1. Install: `build-essential git libsqlite3-dev libxml2-dev libjpeg-dev
   libpng-dev zlib1g-dev libfreetype-dev avahi-daemon avahi-utils
   libavahi-client-dev`, then `sudo systemctl enable --now avahi-daemon`.
2. Copy `DVBCore` and `NippApp` side by side (e.g. `~/DTVkit/`), most simply
   `rsync -a --exclude build ~/DTVkit/DVBCore ~/DTVkit/NippApp user@pc:DTVkit/`,
   which also brings `player/vendor/` (not in git; rebuilding it needs Node.js
   and `~/dvbi-client-js` with the file delivery change). Or clone
   `jmnash16/DVBCore-nip` (branch `dvbnip-gateway`) and `jmnash16/NipApp`.
3. Copy the captures needed (not `1067000.ts` or `1077000.ts`) and write a
   sources file with their absolute paths.
4. Build: `source ./example_setenv.sh && make -j8` in DVBCore, then `make` in
   NippApp.
5. If `ufw` is on: `sudo ufw allow 8080/tcp` and `sudo ufw allow 5353/udp`.
6. Run `./build/nippapp -d <dir> -s <sources> --scan`; the log gives the
   `dvbi_sep` URL announced with mDNS.

Without building there: `tools/package.sh` (here) writes
`build/nippapp-<version>-linux-x86_64.tar.gz` with `bin/nippapp` (optimised,
stripped), `lib/`, `player/` (with `vendor/`), `sources.conf` and
`README.txt`. An executable from this machine fails on a newer Ubuntu
because libxml2 2.14 changed its soname (`libxml2.so.2` to `.so.16`), and ICU
changes its soname with every version. So `lib/` has this machine's libxml2,
ICU, SQLite, libjpeg and libpng. NippApp is linked with an RPATH
`$ORIGIN/../lib`; an RPATH, not a RUNPATH, so that libxml2's own ICU is found
there too. glibc, Avahi with D-Bus and systemd, zlib, liblzma and libstdc++
are taken from the target. Avahi has to match the target's avahi-daemon, and
the others are on every system and backwards compatible. So the target needs
the build machine's glibc or later: package on the oldest distribution to be
supported (24.04 gives glibc 2.38). A server install also needs
`libavahi-client3 avahi-daemon`, or a package built with
`tools/package.sh MDNS=0`. On the target: unpack, put the captures' paths in
`sources.conf` and run `bin/nippapp`, then do step 5. This was tested in a
chroot that has only the system libraries.

A DVB-I client configured by hand (e.g. an STB) is given the entry points,
`http://<gateway address>:8080/dvbi/slep.xml`, or a single list (e.g.
`/ses.com/dvbi/service_list_full.xml`, `/dvb_i_sl_physical.xml`,
`/main_service_list.xml`, `/dvbi/nippapp_list.xml`), always by IP address:
the gateway rewrites `dvb.gw` in what it serves to the address the client
used. Under WSL, a Windows port forward (`netsh interface portproxy add
v4tov4 listenport=8080 listenaddress=0.0.0.0 connectport=8080
connectaddress=<WSL address>` and a firewall rule) or WSL's mirrored
networking reaches the LAN too. With the recordings, a client that takes
"now" from its own clock shows no current programmes (the guide is February
2025), and only plain HTTP is served.

## Startup and scanning

1. `APP_InitialiseDVB(EventHandler, DVB_INIT_NO_TELETEXT_OR_SUBTITLES)`.
2. Set DVBCore's country (`-c`, default `gbr`) if it is not set already. The
   country decides LCN allocation; App asks for it in its first-time setup.
   Without one, no satellite LCNs are allocated.
3. If the database has no services, or `--scan` is given, search each sources
   entry in turn. The first search creates a satellite record ("NippApp") and
   a universal LNB if there is no satellite yet. Each search sets
   `ASI_SetSearchServiceType(SEARCH_SERVICE_TYPE_ALL | SEARCH_SERVICE_TYPE_DVBNIP)`
   (plus `SEARCH_SERVICE_TYPE_DVBNIP_GSE` for PCAP entries) and calls
   `ACTL_StartManualSearch(SIGNAL_QPSK, params, ...)`: `ACTL_FREQ_SEARCH` for
   TS, `ACTL_DVBNIP_GSE_SEARCH` with `is_gse` for PCAP. The main thread polls
   `ACTL_IsSearchComplete()` (as App does on UI update events), with a 180 s
   limit, then finishes the search and the database as App does. The sources
   file stands in for the preset bootstrap stream list of Annex C.4.1.
4. Services found by DVBCore go into the database through `DVBNIP_AddServices`
   (stable IDs, DVB-I names and LCNs); a metadata change later re-runs it via
   `NIPTASK_UPDATESERVICES`.
5. Start the HTTP server (and, in Phase B, the mDNS announcement).

## Serving

Every announcement channel document is kept at its broadcast path (8.2.8), so
`http://dvb.gw/ses.com/dvbi/service_list_entry_point.xml` is served at
`/ses.com/dvbi/service_list_entry_point.xml`. The request path after the host
is always the broadcast path.

| URL | Content |
|---|---|
| `/` , `/player` | the web player |
| broadcast path of the entry points file (`application/xml+dvb-i-slep`) | the service list registry: the broadcast entry points, filtered by TargetCountry, regulatorListFlag, Language, Genre and ProviderName when queried (8.1.7.4); plain and `[]` parameter names both accepted |
| broadcast path of each DVB-I list | the list as broadcast, with `http://dvb.gw/` changed to the gateway's address (Annex F.2.1) |
| broadcast path of the content guide manifest | the content guide endpoint: `?sid&start&end[&inclusive=true]`, `?sid&now_next=true` (Annex D.6.2): a TVAMain of the service's events and programmes; 503 with `Retry-After` while the guide is still being received; 400 for a bad query |
| any other broadcast path | manifests, segments and other files from the file store |
| `/dvbi/slep.xml` | the DVB-I service list entry points, which the broadcast identifies only by URN (`urn:dvb:metadata:nativeip:dvb-i-slep`, 8.2.5.1), so the gateway chooses the URL (8.1.7.3); with several networks, one document offering every network's lists |
| `/dvbi/nippapp_list.xml` | a DVB-I list of the services no broadcast list covers (see below), offered in the entry points when there are any |
| `/time` | the NIP wall clock (8.6.3) |
| any path with `src=DVB-HB` in its query | 501 Not Implemented: a TS broadcast service for a DVB-HB gateway to re-package as DASH (Annex F), which NippApp can't do |
| `/api/status` (GET), `/api/scan` (POST) | admin only: JSON of the version, uptime, scan state, broadcast time, sources, tuners, NIP streams and the services being received on each, file store use, channel count and the networks whose entry points are kept; start a rescan (202, or 409 while one runs). Answered to this machine only (403 otherwise) unless `--admin-from-any` (`-A`). The player never uses these. |

The gateway's address in rewritten URLs is a literal IP address, not a `.local`
name: `dvbi-client-js` allows plain HTTP only to private IP addresses and
localhost.

The registry (`src/registry.c`) keeps a copy of every DVB-I document the NIP
library receives (entry points, service lists, playlists) under `registry/` in
the working directory, at its broadcast path; a document identified by URN is
kept under `registry/urn/`. Its media type is kept beside it. A copy is written
only when the content changes.

Each network's entry points are also kept under `registry/slep/`, named after
their registry entity (`EUTELSAT_DVB-NIP.xml`, `SES_DVB-NIP.xml`), whatever
location they came with, so a new version replaces the old one and other
networks' are kept. `/dvbi/slep.xml` serves one network's entry points as
broadcast; with several, the first by name with every other one's
`ProviderOffering` elements added. Copied offerings keep their own namespace,
which can be an older or newer schema version than the first document's
(Eutelsat's are 2023, SES's 2024); dvbi-client-js ignores namespaces, and a
namespace-aware client sees each offering in its own schema. The server serves the NIP library's copy when
it has one, and otherwise the registry's, at once, so the entry points and
lists are available after a restart and while their carrier isn't tuned
(8.2.8, C.4.3). A request under a path the SIF lists still starts that
carrier (9.5.4), so a list served from the registry is also refreshed from
the broadcast.

When the broadcast has no DVB-I list for some of its services (TP37,
1067000.ts; not conformant), NippApp offers them in a list of its own,
`/dvbi/nippapp_list.xml` ("Services without a broadcast list", provider
"NippApp gateway"), made when requested from the channels no received list
covers (`DVBNIP_ListUnlistedChannels`): each with its DASH and HLS
instances, its channel name and the LCN allocated in the database. The entry
points offer it too, merged like another network's, or on their own when no
broadcast sent entry points at all. A content guide for it is Phase B.

## Request handling and on-demand tuning

As 8.5.3 sets out:

1. Match the request path (ignoring an optional `?serviceId=`) against the SIF.
   No match: 404.
2. Tune the carrier the SIF and NIF give, if not already tuned. This applies to
   any URL in the SIF, not only service manifests: lists, guide files and other
   files can be on other carriers (8.1.7.5, 9.5.4).
3. Serve from the file store. A file not yet received: wait a set time, then 404.
   No tuner free: 503 with Retry-After.
   Carrier can't be received: 503 with Retry-After 30 and "the carrier can't be
   received (no signal)", at once (2026-10-20). DVBCore's control code passes
   STB_EVENT_TUNE_LOCKED and NOTLOCKED to the NIP library
   (`DVBNIP_TunerLockEvent`). A stream tuned for its services whose tuner
   reports not locked before it has locked is released and marked
   (`DVBNIP_IsCarrierUnreceivable`, `noSignal` in `/api/status`). It isn't
   tuned again for 10 s, so the requests meanwhile don't each tune it. One that
   loses lock after locking is kept, as a real tuner may lock again. With the
   recordings this is SES's Inverto 1-3, on SES carrier 1102 (12441 V
   27500), which no capture has; before, they were 404 after 8 s.

A service is started through the existing `nip_mgr` logic (`TuneToNipService`),
stops after 20 s without requests, and its tuner is released 10 s later. The
checks run every 10 s, so in practice a service stops 20-40 s after its last
request and the tuner is released 10-20 s after that. The file tuner offers
more than one tuner path (e.g. 2), so two carriers can be served at once.

The NIP library keeps the SIFs, the NIF and one DVB-I list on DVBCore's disk
(`disks/nip` in the working directory) and rebuilds its service table from
them at startup, so services can be tuned after a restart without a search.
The database doesn't keep an MPE carrier's PID (stream lists come from the
PMT), so after a restart the NIP task finds it once tuned: the service's PMT
from the PAT, then its DSM-CC type D stream (0x0D), as the search does.

A SIF is identified by its provider (8.4.3.1), and each provider can send its
own for the same network, so the NIP library keeps one SIF per provider and
network (up to 8, as `sif.xml`, `sif1.xml`, ...) and the services it can tune
are those of all of them. A SIF received again unchanged is ignored, which
matters with two carriers tuned: both send their SIFs on the same announcement
session, in turn. A provider's SIF is never dropped, only replaced by a newer
one from the same provider.

Pre-buffering (2026-10-20): while a client receives a channel, the NIP
library also receives the other channels on the same NIP stream (carrier),
so a channel change finds their latest segments already in the file store.
The carrier is tuned already, so this needs no other tuner, only CPU and
memory. Each channel is pre-buffered once, in the format of the channel being
watched where it has it (DASH B1 watched: DASH B2 and C1, not their HLS).
A channel left for another stays running, pre-buffered. A pre-buffered
service keeps only 12 s of files, not the usual 30 s. Pre-buffered services
stop together with the last channel a client receives on the carrier, so
the tuner is released as before. `DVBNIP_SetPrebuffer` (off by default in
DVBCore) sets the most channels per stream. NippApp's `--prebuffer` takes
`all` (the default), `off` or a number, in stream order. `/api/status` gives
the setting (`prebuffer`), a count per stream, and `prebuffered` on each
service received.

Measured on SES with B1 watched, a change to B2: without pre-buffering, the
manifest took 2 s and the segment 6 s behind live (where the player starts)
was 404 after 8 s. It was broadcast before B2 was joined, so the player has
to wait for new segments, the 6-7 s seen. With pre-buffering, the manifest
and that segment came in under 0.01 s. Memory on SES: the pre-buffered B2
held 10-23 MB, B1 (watched) up to 43 MB. `tools/asan-check.sh prebuffer`
checks this, `--prebuffer 1` and the release.

The player waited a fixed time before starting dash.js or hls.js (live delay
+ a segment + 1 s, 6-7 s), as without pre-buffering the segments only start
arriving at the join. That wait was all of the 6-7 s still seen after
pre-buffering. So a manifest or playlist now comes with
`X-NippApp-Receiving: <seconds>`, how long its service has been received
(`DVBNIP_GetServiceReceivingTime`), exposed for CORS. The player waits only
for the part of that time not already past, which is none for a pre-buffered
channel. Other DVB-I clients ignore the header; they get the segments at
once too.

A channel signalled in DASH and HLS from one directory (SES's
`livesim2/B1/Manifest.mpd` and `playlist.m3u8`) has two services with the
same directory. A request for a file that is neither's manifest (a segment,
an HLS media playlist) went to the first in SIF order, so a DASH player's
segments also started the HLS service (fixed 2026-10-20). `FindServiceForPath`
now prefers the service being received on such a tie: one a client receives
or has started, then a pre-buffered one being received, then one starting.

Served MPDs list only segments actually available (9.1.2):
`minimumUpdatePeriod` 2 s, the first MPD held until the client's live point
lies after the join, and the AAC config fix from the test tool.

## Clock

The NIP wall clock is the NTP time in the EXT_TIME LCT extension (type 2) of
FLUTE/ROUTE packets; the bootstrap stream's announcement channel is the
reference (8.6.1). NippApp serves it at `/time` as xs:dateTime, with the
`iso`, `xsdate` and `ms` query forms (8.6.3), and inserts or replaces
`UTCTiming` in served MPDs with `urn:mpeg:dash:utc:http-xsdate:2014` pointing
at it (9.1.2). This is one path for MPE and GSE. TDT/TOT, or the capture timestamps of a PCAP,
stay as a fallback when no EXT_TIME has been received.

DVBCore's `flute.c` decodes the EXT_TIME sender current time of every packet
and the NIP library keeps the wall clock from it (`DVBNIP_GetWallClock`),
moving it only when it is out by more than 0.5 s. All four captures carry
EXT_TIME. The Eutelsat capture has no TDT/TOT at all, so before this its
players were given another capture's time and asked for segments that never
arrive ("stream initialised", then nothing).

With recordings, two carriers from different recordings have different
times, so while both are tuned the clock follows whichever sent EXT_TIME last.
That can't happen with live reception, so the gateway keeps one clock (decided
2026-10-13). When testing with captures, use one capture at a time, or
captures with the same clock. For example, Inverto's `1097000.ts` runs at
about 10:46 and Eutelsat's `1087000.ts` at about 10:33. While VandVA1's
carrier is still tuned (30-60 s after its last request), `/time` gives
Eutelsat's time, and VandVA2 stops at "stream initialised", asking for
segments 13 minutes old.

## Player

`player/player.html` is a DVB-I client: it uses `dvbi-client-js` (a pure
JavaScript DVB-I client, `~/dvbi-client-js`) to read the entry points at
`/dvbi/slep.xml`, offers the service lists they name, lists the chosen list's
services in LCN order with their logos, and plays a service's DASH instance
with dash.js or its HLS instance with hls.js. NippApp serves it at `/player/`
from the `player/` directory next to `build/` (or `-P`).

`tools/build-player.sh` makes `player/vendor/`, which is not kept in git: it
builds dvbi-client-js from its checkout (`npm ci --ignore-scripts`, vite) and
fetches dash.js 4.7.4 and hls.js 1.5.20 from npm, with their licences. It needs
Node.js 20.19 or newer and the npm registry the first time; after that the
gateway needs no internet access to serve the player.

Starting a service: the page fetches the manifest first, which tunes the
carrier; the gateway answers once the manifest has arrived. Only segments that
start after joining are received, so the page waits the live delay (4 s), one
segment and a margin before starting the player. dash.js takes its clock from
`/time`; hls.js plays the live delay behind the playlist's end. These settings
are the test tool's.

In dvbi-client-js's results a service instance's delivery is in
`instance.deliveryParameters` (`type` `dash` with `dash.mpdUrl`, `hls` with
`other.url`, or `file` with `other.url` and `other.mimeType`).

Offline content: SES's "DVB-NIP Offline Content" list has, besides B1, B2 and
C1, 12 pictures, 10 videos and 11 PDFs from its object carousels, each an
`IdentifierBasedDeliveryParameters` instance whose identifier is the file's
URL and whose `contentType` is its MIME type, with a cover as its banner
(1001.3). dvbi-client-js dropped those instances (it only knew HLS, Icecast
and MBMS there); it now gives an HTTP(S) URL with a MIME type as delivery
type `file` (`fileDelivery` in `src/service-list.js`, with a parser test).
The player shows a file's button by type (Picture, Video, PDF), uses a
service's banner as its thumbnail when it has no logo, and on choosing one
requests it until it arrives (404 or 503: tried again every 5 s, or after
`Retry-After`, for up to three minutes, as a carousel can take a minute or
more to come round). A picture is shown in place of the video, a video is
played as a file by the video element, and a PDF is shown in a frame (a new
tab opened after waiting would be stopped by pop-up blockers). The pictures
and covers that are complete in the SES capture are shown (a picture after
up to a minute). The videos (17-291 MB) and PDFs (30-115 MB) can't be
delivered yet: the capture holds at most 16 MB of each, the file store keeps
files in memory with limits of 128 MB per service and 256 MB in all, and
seeking in a video needs byte range requests, which the server doesn't
answer.

A DASH instance whose URL has `src=DVB-HB` in its query (VandVD1 in
Eutelsat's list) is a TS broadcast service that a DVB-HB gateway would tune
and re-package as DASH (Annex F, informative; DVB A179). The player labels its
button "DVB-HB" rather than DASH, and still plays it as DASH. NippApp has no
DVB-HB function, so it answers any request with `src=DVB-HB` in its query at
once with 501 Not Implemented, and the player says why.

## Discovery

DNS-SD/mDNS is mandatory for a DM3 gateway (11.1). NippApp registers through
avahi-daemon (`src/mdns.c`, libavahi-client) a `_dvbservdsc._tcp` and an
`_http._tcp` service on each IPv4 interface of the LAN, named "NippApp on
<hostname>" (`--name` to choose; Avahi picks "... #2" on a clash), on the HTTP
port. The `_dvbservdsc._tcp` TXT record has `txtvers=1`,
`dvbi_sep=http://<that interface's address>:<port>/dvbi/slep.xml` (a literal
address, as dvbi-client-js allows plain HTTP only to private addresses),
`manuf=DTVKit`, `model=NippApp` and `tuners=DVB-S2/<tuners>`; the
`_http._tcp` one has `path=/`. The keys are separate TXT strings as in RFC
6763, which the clause cites; its `"k1=v1;k2=v2"` example is taken as
notation (to check). `orb_pos_A` isn't given, as the file tuner has no real
orbital position. Without avahi-daemon NippApp says so and announces once it
starts; `--no-mdns` (`-M`) leaves the announcement out, and a build without
libavahi-client-dev (or `make MDNS=0`) has none. Browser clients cannot use
mDNS; they load the player from the gateway. Under WSL with NAT networking
the announcement stays inside the WSL VM (mirrored networking, or a native
Linux host, reaches the LAN).

## Milestones

| # | Milestone | Done when |
|---|---|---|
| 1 | Skeleton | NippApp builds and links against `libdvbcore.a`, starts and stops cleanly, creates the database |
| 2 | TS input + MPE search | a search on `1097000.ts` puts A2/B2 in the database with names, LCNs and stable IDs; a rescan creates no duplicates |
| 3 | PCAP input + GSE search | a search on the SES PCAP finds B1/B2/C1 over the GSE path |
| 4 | HTTP, registry, player | the player starts from the broadcast entry points served by NippApp, loads a broadcast list and plays DASH, and HLS when the list signals it; tuning happens on request and is released when idle |
| 5 | Robustness | two carriers at once, tuner contention, metadata version change updates the list, AddressSanitizer clean |
| B | DVB-I endpoint | entry point queries, mDNS, content guide (service guide and content guide), works with an independent DVB-I client |

## Not yet supported by DVBCore

Mandatory for a DM3 gateway, none implemented yet: ROUTE reception, compressed
signalling files (FDT Content-Encoding), 48-bit TOIs (`flute.c` keeps 32 bits)
and mDNS. For GSE only: ROHC and GSE-Lite link-layer signalling. All our
captures use FLUTE with Content-Encoding null for signalling. (EXT_TIME is now
decoded; see Clock. Files the gateway serves are decoded by NippApp; see
Milestone 5 progress, object carousels. The NIP library itself still can't
parse a compressed NIF, SIF, list or gateway configuration.)

## Open points

- A PCAP has no tuning information, so its sources entry is made up. The
  NACI gives its identity (SES: network 1, carrier 1045), but there is no
  check yet that the NIF's parameters for that carrier match the entry.
- DVBCore has one LCN space, so lists that ask for the same LCNs clash: with
  Inverto and SES searched together, SES's B1 and B2 get 1003 and 1004. The
  registry (milestone 4) serves each broadcast list with its own LCNs, so
  clients are not affected.

## Milestone 2 findings

- A missing NIT is not a problem: the search times out on the NIT and goes on.
- The Inverto captures' SDT has no data_broadcast_descriptor, so DVBCore never
  started the NIP scan. DVBCore's search now also recognises the MPE stream by
  its PMT stream type 0x0D, as DVB-NIP clause 7.3.3 requires (`ap_si.c`,
  `FindPmtMpePid`). The PMT is read before `ProcessPmtTable`, which frees it.

## Milestone 3 findings

- The GSE search works through the PCAP input with no change to DVBCore's GSE
  code: the SES capture gives B1, B2 and C1 (LCNs 1-3) and Inverto 1-3 (LCNs
  5-7, on another carrier in the SIF), TP37 gives s0, s1 and B1. A rescan
  creates no duplicates; an AddressSanitizer run over TS and both PCAPs shows
  no errors or leaks.
- DVBCore ran a manual GSE search as a network search
  (`ACTL_StartManualSearch` sent `ACTL_DVBNIP_GSE_SEARCH` to the
  `SEARCH_TYPE_MANUAL_NETWORK` branch). After the first carrier it retuned
  every transport in the database as a TS channel search, including NIP
  transports from the NIF and SIF. It is now a frequency search of the one
  carrier (`ap_cntrl.c`).

## Milestone 4 progress

Done so far:

- The web player (`player/player.html`, `tools/build-player.sh`), served at
  `/player/`. SES's B1 manifest and HLS playlist are both served on demand.
- A short capture should loop in the sources file: a tuner still on a capture
  that has ended has nothing for the next request (the SES PCAP is 64 s).
- Eutelsat (`1087000.ts`): its headend sends an FDT instance per file, so the
  announcement carousel looked complete before the entry points and the DVB-I
  list arrived; the scan ended without them, the registry had nothing to offer
  and the channels got fallback names and LCNs. DVBCore's NIP scan now waits
  for the entry points and a DVB-I list as well as the NIF and SIF (a bootstrap
  stream must carry them, clause 8.1.4.1); a stream without them is completed
  by the 20 s scan timeout, so searching TP37 or `1067000.ts` takes about 20 s.
  Eutelsat's channels now get their DVB-I names and LCNs (VandVA1 at 11, ...).
- Eutelsat also sends its entry points and list with bare Content-Locations
  (`dvb_i_sl_ep.xml`, `dvb_i_sl_physical.xml`) rather than the URN and
  `http://dvb.gw/` URLs of clause 8.2.7. The registry takes a bare name as a
  path from the root, and keeps any entry points document as the gateway's
  entry points too, whatever its location. `/dvbi/slep.xml` serves the
  registry's copy first, as the NIP library may still hold another network's
  under the URN.
- The NIP library kept one SIF: the last one received. Inverto and Eutelsat
  each send their own SIF for network 318 (Inverto's lists only carrier 801,
  Eutelsat's both 601 and 801), so playing VandVA2 replaced Eutelsat's SIF and
  Eutelsat's services could not be tuned until a rescan. DVBCore now keeps a
  SIF per provider (`nip_mgr.c` `MergeSif`; `nip_xml.c` returns the
  `NIPNetworkProviderName`, or the older `ProviderName`). With Inverto and
  Eutelsat searched, VandVA2 and VandVA1 play together for a minute with both
  SIFs arriving in turn, without the service table changing; AddressSanitizer
  is clean over a scan, both carriers, a restart and shutdown.
- MPE services could not be tuned after a restart without a rescan (found
  while testing the SIF merge): `TuneToNipService` (`nip_task.c`) took the MPE
  PID from the carrier service's stream list, which the database doesn't keep,
  so nothing was tuned and the request got 404 after 8 s. GSE needs no PID, so
  SES was unaffected. Now, when the PID isn't known, the task tunes, filters
  the PAT for the service's PMT and the PMT for its type 0x0D stream, and then
  starts the MPE filter (Eutelsat 7011, Inverto 300, as the search finds).
  Tested after a restart with both carriers, after an idle release and
  re-tune, and with AddressSanitizer.
- The gateway offers the entry points of the network searched last. Inverto's
  `1097000.ts` and Eutelsat's `1087000.ts` are one network (318), and
  Eutelsat's SIF and list cover both carriers: searched in that order, the
  player offers Eutelsat's list and plays services on both carriers, one tuner
  each (VandVA1 and VandVA2 at the same time).

- The registry keeps every DVB-I document received on disk; after a restart
  `/dvbi/slep.xml` and the lists are served at once, with no tuning.

- The HTTP server (`src/httpd.c`, mongoose 3.8 from the test tool, port `-p`,
  default 8080) serves files at their broadcast paths, waits up to 8 s for
  broadcast files not received yet, rewrites `http://dvb.gw/` in DVB-I
  documents to the gateway's address, rewrites MPDs (UTCTiming to `/time`,
  `minimumUpdatePeriod` 2 s), applies the AAC fix, sends CORS headers on every
  response, and serves `/time`, `/dvbi/slep.xml` and an index page.
- `/time` is the NIP wall clock from EXT_TIME (`DVBNIP_GetWallClock`, see
  Clock), falling back to the file tuner's broadcast clock (TDT/TOT of TS
  captures, timestamps of PCAP captures). This fixed Eutelsat playback stopping
  at "stream initialised".
- Playback in a browser is confirmed: SES's DASH and HLS, and Eutelsat's
  VandVA1 and VandVA2 together on two tuners.
- On-demand tuning works: a request for SES's B1 manifest tunes the SES PCAP
  in about 4 s; segments after the join are held until they arrive; the service
  stops and the tuner is released when requests stop, also after a restart.
- DVBCore fix (`nip_task.c`): the NIP task released its tuner path without
  stopping the tune, so the tuner kept running. It now calls `ACTL_TuneOff`
  first, which stops the tuner unless another path uses it. (The first version
  called `STB_DPStopTune`, below ACTL: ACTL still took the path as tuned to the
  transport, so a service requested again after its tuner was released was
  never re-tuned and answered 404.)

Done later (2026-10-13):

- HLS: DVB-I has no standard HLS delivery for NIP (DASH is the only mandatory
  format, 9.1; NIP services use DASHDeliveryParameters, 8.3.3.2). Decided
  2026-10-13: the player keeps its HLS button, shown beside DASH whenever the
  list signals an HLS instance, and milestone 4's criterion is "plays DASH,
  and HLS when the list signals it". `nip_xml.c` now takes an
  IdentifierBasedDeliveryParameters instance as HLS only when its
  `contentType` is `application/vnd.apple.mpegurl` (or
  `application/x-mpegURL`); others are skipped. Every HLS instance in the
  captures has that type.

Still to do:

- One NIF: the NIP library keeps the last one received. It only adds
  transports to the database, so nothing is lost when it is replaced.
- Large carousel files (SES's videos and PDFs): keep them on disk rather than
  in memory, answer byte range requests, and test with a capture that holds
  them complete. (A carousel file still on its way is now answered 503 with
  `Retry-After: 10`; a later refinement could take the delay from the
  carousel's `targetAcquisitionLatency`.)
- VandVD1 (DVB-HB) and OTT instances can't be played through the gateway.

## Milestone 5 progress

Tuner contention (8.5.3):

- DVBCore: when the NIP task can't get a tuner path, it now reports it
  (`DVBNIP_TaskTuneFailed`). Before, the stream stayed marked as tuned, so the
  request waited 8 s for a 404 and the service couldn't be tried again until
  the next garbage collection.
- If another carrier's tuner is only waiting to be released (no service
  playing, starting or requested in the last 5 s), it is released at once and
  the tune tried again, rather than after the usual 10 s.
- Otherwise the carrier is marked as having no tuner until a tuner is freed or
  a new request tries again, and `DVBNIP_IsTunerUnavailable(path)` says so.
  NippApp answers 503 with `Retry-After: 10` (exposed to other origins), at
  once rather than after 8 s.
- The player retries after `Retry-After` while the service is still selected,
  saying that no tuner is free.
- Tested with Inverto, Eutelsat and SES on the two tuners: B1 gets 503 while
  VandVA1 and VandVA2 are in use; about 24 s after VandVA2's last request its
  service stops, and the next B1 request takes its tuner. AddressSanitizer
  clean.

Metadata version change:

- Tested with two Eutelsat captures of carrier 601: `1087000.ts` (April 2024)
  and `20240902Hbtxp6_1mn.ts` (September 2024, services moved under
  `/eutelsat/`). Searched with the first, then restarted without a search on
  the second.
- DVBCore bug fixed (`nip_task.c`): every on-demand tune started a scan
  (`DVBNIP_AddFluteSessionBootstrap`), which left the NIP library waiting for a
  search to finish, so a new SIF or DVB-I list updated the service table and
  the served list but never the channels in the database, and the metadata
  monitor was never stopped. A tune for services now only starts the metadata
  monitor (`DVBNIP_StartMetadataMonitor`).
- Now the new SIF replaces the old one (same provider), April's channels are
  deleted, the `/eutelsat/` channels are added with LCNs 11-13 from the new
  list, and the database is saved; the old paths answer 404 and the new ones
  play.
- DVBCore bug fixed (`ap_dbdef.c`): deleting a service loaded from the
  database leaked its name (`name_str`); found by AddressSanitizer here, which
  is now clean.

Several networks and lists:

- NippApp keeps each network's entry points and serves them merged (see
  Serving). With Inverto, Eutelsat and SES searched, `/dvbi/slep.xml` offers
  Inverto's "Airstream Main List", Eutelsat's list and SES's three, and
  dvbi-client-js loads each (2, 7, 6 and 3 services; SES's offline content
  list is 404, above).
- DVBCore keeps every DVB-I list, by its location, as `dvbisl.xml`,
  `dvbisl1.xml`, ... with the location beside each (`dvbisl.loc`, ...); a list
  saved before this, without a location, isn't used. A service takes its name,
  identifier and LCN from the first list that has it.
- DVBCore bug fixed: `DVBNIP_PathMatch` ignores a host-like first segment (for
  TP37's `/10.0.1.229/...`), so SES's `/ses.com/livesim2/B1/Manifest.mpd`
  also matched Eutelsat's `/livesim2/B1/Manifest.mpd`: SES's B1 and C1 were
  named after Eutelsat's, a request could find the other network's service or
  stored file, and a gateway configuration's sessions could be given to the
  other network's service. Lookups (`FindServiceByURI`, `FindServiceForPath`,
  `DVBNIP_FindFile`, the list join and `AddMulticasts`) now look for the same
  path, ignoring case only (`DVBNIP_PathEqual`), before the looser match.
- Now SES's B1, B2 and C1 keep their names (B1 and B2 at LCNs 1003-1004, as
  Inverto's A2 and B2 have 1 and 2), each network's B1 tunes its own carrier,
  and the April to September Eutelsat change still replaces the list and the
  entry points. AddressSanitizer clean over a three-network search, a restart
  and the change.
- The player collapses white space in names (Eutelsat lays them out over
  several lines).

Fast 404 and hidden carrier services:

- A request for a path that no SIF lists, nor any directory or service it
  belongs to, is answered 404 at once (8.5.3) rather than after the 8 s wait
  (`DVBNIP_IsPathSignalled`). Eutelsat's logos (`/materials/...`, never
  broadcast) now fail in under 1 ms.
- Each NIP stream's own service (DVB-NIP-VV, which carries the MPE) is hidden
  and not selectable in the database, so it is no longer offered as a channel
  (`UpdateChannels`). The legacy TS service is still listed.

AddressSanitizer pass: `tools/asan-check.sh` builds NippApp with
AddressSanitizer, runs it through the scenarios on the captures and checks the
results and that every run ends with no AddressSanitizer error or leak, then
rebuilds the normal binary. About 15 minutes; `tools/asan-check.sh scans`,
`networks` or `change` runs one scenario, `KEEP=1` keeps the logs.

- scans: each capture searched on its own (Inverto, Eutelsat, HBtxp6, SES,
  TP37; `1067000.ts` is left out as 9 GB, `1077000.ts` as a copy of
  `1097000.ts`), with a minimum number of channels each.
- networks: Inverto, Eutelsat and SES on two tuners: names, hidden carrier
  services, three SIFs and entry points, merged `/dvbi/slep.xml`, 503 with
  both tuners in use, 501 for DVB-HB, fast 404; then a restart without a
  search, with each B1 tuning its own carrier.
- change: Eutelsat April to September 2024: the old path gone, the new one
  playing, new entry points, new channels in the database.

33 of 33 checks pass.

Object carousels (A180 8.2.6):

- SES sends much of its content in object carousels: separate FLUTE sessions
  that the announcement channel's gateway configuration declares as
  `MulticastGatewayConfigurationTransportSession`s, each with a `tags`
  directory and an `ObjectCarousel` of `ResourceLocator`s. There are 14: the
  content guide schedule and its 285 images, the service guide, logos and
  banners, SES's offline content list, pictures, VOD files, PDFs and their
  covers, and an HbbTV application. The NIP library parsed only their
  endpoints and joined them as bootstrap sessions, keeping nothing but
  gateway configurations and manifests from them.
- `nip_xml.c` now also reads each one's tags and resource locators, as a
  carousel entry after the usual bootstrap entry. When a configuration is
  received, a SIF entry matching a carousel's tags or one of its resources
  takes the carousel's session, as a service takes its media sessions, and
  remembers the resources, so a file the SIF doesn't list itself (SES's
  schedule sits beside its tags directory `/ses.com/dvbi/cg/CoreSet`) still
  finds its carousel. A request then tunes the carrier, joins the session and
  stores what arrives. The announcement channel's own carousel is left to the
  metadata monitor.
- A SIF entry whose last segment has no `.` is a directory, and covers only
  what is under it: before, `/ses.com/private/pictures` covered all of
  `/ses.com/private/`, so a picture could start the PDF carousel.
- A carousel's files are kept until replaced or over the memory limits, not
  dropped after 30 s, as a carousel can take a minute or more to come round
  (SES's pictures carousel is 4.6 MB).
- NippApp encodes the request path again as RFC 3986 does (mongoose decodes
  it, and broadcast names keep their encoding: `P.M.%20Magazin.jpg`), waits
  for any path a SIF lists rather than only for some file extensions, and
  decodes files sent compressed (`DVBNIP_CopyFileEncoded` gives the FDT's
  Content-Encoding; gzip, zlib and deflate; `src/encoding.c`), as SES sends
  its schedule gzip (87 KB, 959 KB decoded). The registry keeps compressed
  DVB-I documents decoded.
- Now SES's offline content list arrives in about 3 s (and the registry keeps
  it), the covers, the service guide and the 959 KB TV-Anytime schedule are
  served, and complete pictures arrive when their carousel comes round (one
  after 81 s). Five of the twelve pictures are only partly in the 64 s
  capture (e.g. `caleb-smith.jpg`), so those never arrive.
- `tools/asan-check.sh carousels` checks the list, an encoded name and the
  decoded schedule.

Re-tune (2026-10-15):

- After a tuner was released, the first request for a service's HLS media
  playlist was answered at once with the copy kept from the last session,
  listing segments from before the release that were still in the store: a
  client joining then was sent into old content, then a gap. (The SES
  capture also restarts on re-tune, moving its clock back a minute.)
- DVBCore now doesn't serve a manifest or playlist that lists its segments
  (an HLS media playlist, `#EXTINF`; a DASH `SegmentTimeline`) received
  before its service last started (`received_time` on each file, never
  cleared, refreshed when an unchanged copy arrives; `started_at` on each
  service); the request waits for the fresh one, about 2 s for SES. Documents
  that don't change (DASH `SegmentTemplate` manifests, HLS master playlists)
  are served at once as before: Eutelsat sends its manifests only on the
  bootstrap session, so they are never received again once a service plays.

A list for broadcasts without one (2026-10-15): see Serving. TP37's s0 (HLS),
s1 and B1 (DASH) are offered at LCNs 1000-1002, and dvbi-client-js loads
them.

`tools/asan-check.sh` has two more scenarios: `nolist` (TP37: the gateway's
list offered, three services, their manifests served) and `retune` (SES: the
HLS media playlist after a re-tune lists segments within 20 s of the clock).

Admin API (2026-10-15): `/api/status` and `/api/scan` (see Serving). A scan
asked for runs on the main thread, as the first one does, and can run while
services play (tested with A2 playing: every source searched, 12 channels,
all three carriers playing afterwards). New DVBCore calls
`DVBNIP_ListStreams` and `DVBNIP_GetStoreUsage`; the file tuner reports each
tuner's state (`TUN_GetTunerState`). `tools/asan-check.sh networks` checks
the status JSON, 202 and then 409 from `/api/scan`, and that the scan
completes. That rescan found another DVBCore leak: deleting a service didn't
free the PMT it keeps for CA systems (`pmt_data`, `ap_dbdef.c`); fixed.

Still to do for milestone 5: two carriers at once with captures that share a
clock (needs new captures).

## Phase B progress

Entry point query filtering (8.1.7.4, 2026-10-15): `/dvbi/slep.xml` (and a
broadcast entry points file at its own path) filtered by `TargetCountry`,
`Language`, `Genre`, `ProviderName` and `Delivery` (each as `Name` or
`Name[]`, repeatable) and `regulatorListFlag=true`, as dvbi-client-js sends
them (`REG_FilterEntryPoints`, `src/registry.c`). Every parameter given must
match, any of its values; providers are matched by `Provider/Name`, ignoring
case and surrounding white space; the rest by each `ServiceListOffering`'s
`TargetCountry` codes, `Language` (a tag matches a more or less specific form
of itself, `en` with `en-GB`), `Genre` hrefs, `Delivery` children
and `regulatorListFlag`; a provider left with no offering is removed.

Checked against TS 103 770 clause 5.1.3.2 (2026-10-15) and corrected: an
offering that doesn't declare a property matches whatever is asked for it
(Inverto's list, which declares no country, delivery or language, is
returned for `TargetCountry=LUX` or `GBR`); a parameter the registry
interface doesn't define, or a value that isn't valid, is answered 400
(`REG_BAD_QUERY`); `Delivery` values are those of table 12b (`dvb-dash`,
`dvb-t`, `dvb-c`, `dvb-s`, `dvb-iptv` for `MulticastTSDelivery` or
`RTSPDelivery`, `application`); `TargetCountry` must be a three-letter code;
`regulatorListFlag` and `inlineImages` must be `true` or `false`.
`regulatorListFlag=false` applies no filter. `inlineImages=true` is
accepted, but images are still given as URLs, which the gateway serves from
the broadcast. Tested by hand and with dvbi-client-js's discovery parameters;
`tools/asan-check.sh networks` checks a provider and a country query, an
undefined parameter and a Delivery value outside the table.

Content guide endpoint (Annex D, 2026-10-15): a DVB-I client asks the
service list's `ScheduleInfoEndpoint`, which for a broadcast list is the NIP
Content Guide Manifest (`http://dvb.gw/ses.com/dvbi/cg/manifest.xml`), with
`?sid=<ServiceIDRef>&start=<s>&end=<s>`, `?sid=...&now_next=true` or
`?sid=...&now_next=window` (`src/guide.c`). The manifest's `GuideSession` `TagRef`s are the tags of the
object carousels carrying the guide (SES: `/ses.com/dvbi/cg/CoreSet`, a gzip
TV-Anytime schedule of 534 programmes over four weeks, and its images); the
stream's NIP Service Guide Manifest (`urn:dvb:metadata:nativeip:ServiceGuide`)
names the carousels with now and next (`/ses.com/dvbi/sg/CoreSet`). Both
carry TVAMain documents already in DVB-I's form, so the answer is a TVAMain
with the service's events from all of them (sorted, the same event in both
once) and their ProgramInformation, its `dvb.gw` URLs rewritten. A schedule
query returns the events starting in `[start, end)`, with `inclusive=true`
also the one under way at `start`; now and next are the event on at the
broadcast time and the one after it. The documents' files come from the
carousels (new DVBCore call `DVBNIP_ListCarouselResources`), decoded and kept
parsed until their content changes; asking for them tunes and joins, and
until they are here the answer is 503 with `Retry-After: 10`. Tested by hand
(now and next at 14:21 broadcast time: "Ingo Thiel - Briefe aus dem
Jenseits", then "Leben mit Vulkanen"; seven programmes 18:00-24:00) and with
dvbi-client-js's `epg.getEvents`. dvbi-client-js chooses now and next by its
own clock, so with these 2025 recordings its `getNowNext` finds nothing
current, though the gateway answers; live reception has no such gap.
Checked against TS 103 770 clauses 6.5.2-6.5.4 (2026-10-15): the window rule
is as specified. Corrected: `start` and `end` must be whole 3 hours, 6 or 12
hours apart, within 28 days of today by the broadcast clock, or the answer
is 400; `now_next=window` gives the current event with up to ten before and
after (the 16 SES has for B1 at 14:21), any other `now_next` value 400; a
service no guide document has a Schedule for gets empty tables, one that has
a Schedule but no events in the window an empty Schedule. Other parameters
(`image_variant`, `regionID[]`) are ignored, which the clause allows.
`tools/asan-check.sh carousels` checks now and next, a window, a window
query, a bad query, a misaligned window and an unknown service.

A carousel's file that hasn't come round within the 8 s wait is answered 503
with `Retry-After: 10` and the body "waiting for the broadcast carousel",
rather than 404, so a client asks again (`DVBNIP_IsCarouselPath`). Every 503
now says why in its body ("no tuner free", "receiving the content guide"),
and the player shows that reason as it retries. Tested: `brayden-prato.jpg`,
only partly in the SES capture, is 503 each time; `anna-kasin.jpg` arrived
on the 6th request, after 50 s. Of SES's twelve pictures, four are known to
be complete in the capture (anna-kasin, fabrice-villard, marek-piwnicki and
vadym-chuma); haberdoedas wasn't identified in that check.

mDNS announcement (11.1, 2026-10-15): see Discovery. Tested with
avahi-browse: "NippApp on Zen" as `_dvbservdsc._tcp` and `_http._tcp` on
eth0 with the TXT keys above, its `dvbi_sep` URL serving the entry points; a
second gateway renamed "NippApp on Zen #2"; both withdrawn when stopped.
`tools/asan-check.sh networks` checks the announcement when avahi-browse is
installed.

civetweb (2026-10-15): the HTTP server is now civetweb 1.16, the maintained
MIT fork of mongoose (later mongoose releases are GPL), in
`third_party/civetweb/`, built without TLS, CGI and file serving. The API is
mongoose's, so `src/httpd.c` changed little: the request path is `local_uri`
(URL-decoded, as before, so it is still encoded again), the client address
is a string (the admin API's local check accepts 127.x.x.x, `::1` and
IPv4-mapped addresses), and the library is started and stopped with
`mg_init_library` and `mg_exit_library`. Every request is still answered by
NippApp's own handler.

## STB Implementation

Reviewed 2026-10-20: can NippApp run on a set-top box, given front-end
drivers? Nothing in the design prevents it. All the hardware is behind
DVBCore's platform API, the same interface DVBCore uses on STBs; the file
tuner and demux are NippApp's implementations of it. With the STB's tuner and
demux functions in their place, the DVB-NIP library and NippApp's application
modules are unchanged. To be revisited after disk caching. What has to be
done or decided, most significant first:

1. **Sharing tuners with the STB's own TV software.** The one real design
   question. If the box also runs a TV application (e.g. DVBCore's App with a
   UI), two DVBCore instances in two processes would each think they own the
   tuners and demux. Either build the gateway into the STB's DVBCore
   application as one process (DVBCore's resource manager arbitrates; NIP
   streams already ask for tuners at `DP_PRIORITY_LOW`, so live TV wins;
   NippApp's modules become a library in that application rather than a
   separate `main()`), or reserve tuners for NippApp. A headless gateway box
   doesn't have this question.
2. **Platform layer for the real hardware.**
   - Replace `stbhwtun.c` and `stbhwdmx.c` with the SoC vendor's tuner and
     demux functions, or the Linux DVB API. The tuner must report lock with
     `STB_OSSendEvent(HW_EV_CLASS_TUNER, ...)` as the file tuner does; the
     no-signal and no-tuner handling depend on those events.
   - GSE carriers: `GSE_DataIn` expects GSE packets, so the demodulator must
     deliver the carrier as a generic stream (not TS) and the driver or
     platform layer must strip the base-band frame headers. Many STB
     demodulators and drivers only output TS: check the target hardware.
     MPE carriers need nothing extra (ordinary section filtering).
   - Throughput: MPE at 50-60 Mbit/s through section filters is heavy for
     some hardware demuxes; check the SoC's section filter buffers keep up.
   - Take the other files that touch hardware or the OS (OS layer, disk,
     network) from the STB's existing DVBCore port; NippApp's are mostly
     copies of DVBCore's Linux reference ones.
3. **What the file tuner stands in for.**
   - Dish setup: `search.c` creates a satellite record "NippApp" with a
     universal LNB. A real installation needs the LNB type, DiSEqC or
     Unicable, and the satellite position. There is no setup UI, so this needs
     a configuration file or options.
   - The bootstrap carriers to search (Annex C.4.1): the sources file does
     this now; the same format without the file columns would serve as the
     preset list.
4. **Resources.**
   - RAM: the file store uses DVBCore's default limits (256 MB in all,
     128 MB per service) and NippApp has no option to change them.
     `DVBNIP_SetFileStoreLimits` exists, so an option is a small addition;
     needed on a box with 1-2 GB. Pre-buffering stays within the limits, but
     on a small box `--prebuffer` should probably default to a few channels.
   - CPU: FLUTE reassembly of a whole carrier is in software, and each HTTP
     response copies the file out of the store. Only measured on a desktop;
     profile on the STB's processor.
   - Flash wear: the registry writes only on change and the database only at
     a scan or shutdown, so these are fine. For disk caching: live segments
     stay in RAM, and the disk store should be on a USB disk or HDD rather
     than eMMC.
5. **Build and system integration.**
   - Cross-compiling: the Makefile assumes the native compiler and
     `/usr/include/libxml2`; it needs a cross-compiler, a sysroot and
     pkg-config settings from the STB SDK (Buildroot or Yocto provide libxml2,
     SQLite, zlib, libjpeg and libpng).
   - 32-bit ARM: DVBCore is built for 64-bit at present. No casts of pointers
     to 32-bit types were found in the NIP library or NippApp, but it hasn't
     been built or run as 32-bit; running `tools/asan-check.sh` on a 32-bit
     build would answer that.
   - mDNS: Avahi is optional. Without Avahi on the box, build with `MDNS=0`
     or support the box's mDNS stack (e.g. mDNSResponder).
   - Running as a service: an init or systemd script, logging to syslog
     rather than stdout, a writable working directory.
   - Security: plain HTTP, no authentication, on all interfaces. Fine on a
     home LAN; on a box with a WAN interface it should bind to the LAN
     interface only, for which there is no option yet.
6. **Not yet supported in DVBCore, and possibly needed live:** ROUTE,
   compressed signalling files, 48-bit TOIs, ROHC and GSE-Lite link-layer
   signalling. The recordings don't need them, but a live GSE carrier may use
   GSE-Lite or ROHC. MPE carriers are exempt (Annex A), so an MPE carrier is
   the safest first live test.

Suggested route:

1. A TS-output STB tuning an MPE carrier, with NippApp as the box's only DVB
   application.
2. The small configuration options: LNB and satellite, file store limits,
   the address to bind to, the bootstrap carriers.
3. Then GSE output from the demodulator, tuner sharing with a TV
   application, and 32-bit builds, as the target needs.

## Resume here (state at 2026-10-20)

Committed: milestones 1-5 (but the same-clock test) and Phase B, including
civetweb and the corrections from checking TS 103 770.

Not committed yet:

- Pre-buffering: DVBCore `dvbnip/inc/nip_mgr.h`, `dvbnip/src/nip_mgr.c`;
  NippApp `src/main.c`, `src/admin.c`, `inc/nippapp.h`,
  `tools/asan-check.sh`, this file.
- dvbi-client-js (`~/dvbi-client-js`, remote `DTVKit/dvbi-client-js`):
  `src/service-list.js`, `test/parsers.test.js` (file deliveries, which the
  player needs for SES's offline content).

The DVB-I specification is in `DVBCore/documents/ts_103770v010201p.pdf`;
`DVBCore/documents/` and DVBCore's `dvbnip/test/DVB-NIP-DVB-Iplan.md` and
`dvbnip/test/DTVkit.code-workspace` are local files, not to be committed.

To try Inverto + Eutelsat, a sources file with absolute paths, Inverto first:

```
11471 V 27500 ts /home/jason/DTVkit/Streams/DvbNIP/1097000.ts loop
11013 H 27500 ts /home/jason/DTVkit/Streams/DvbNIP/1087000.ts loop
```

then `./build/nippapp -d <dir> -s <file> --scan` and open
`http://<ip>:8080/player/`. Add
`11141 H 23500 pcap /home/jason/DTVkit/Streams/DvbNIP/2025-02-11_1min.pcap loop`
for a third carrier (tuner contention, SES's offline content and content
guide), or use `11023 H 23500 pcap .../TP37_2min_231106.pcap loop` for a
broadcast without a list. Run `tools/asan-check.sh` (about 20 minutes) after
changes.

Milestone 5 still needs captures of two carriers with the same clock.

Next: disk caching of large files, then revisit "STB Implementation" above (agreed design: carousel files over a size
threshold received to a partial file on disk and served from it with byte
ranges; live segments stay in RAM unless asked; `--store-dir`,
`--disk-threshold`, `--disk-limit`; a FLUTE PCAP generator to test it, as no
capture holds large files complete). Then the rest of Phase B: an independent DVB-I client (Jason's STB, its
service list endpoints set by hand; best with NippApp on a separate Linux PC,
see "Running on a separate Linux PC"; the mDNS TXT format is still to
confirm). Optional later: DVB-HB support (VandVD1).
