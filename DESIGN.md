# NippApp design outline

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
    main.c               options, startup, main loop, shutdown
    events.c             DVBCore event handler (logging for now)
    search.c             first-run setup (country, satellite/LNB records) and scans
    registry.c           keeps every DVB-I document received on disk (query filtering: Phase B)
    guide.c              content guide endpoint (Phase 5)
    httpd.c              HTTP server: broadcast files, DVB-I rewriting, /time, /dvbi/slep.xml (milestone 4)
    mdns.c               DNS-SD/mDNS announcement (Phase B)
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
    mongoose/            mongoose 3.8 (MIT), from the test tool; to be replaced by civetweb
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
   when switching between the two.
3. The build defines `DVBNIP_BUILD`, the same as DVBCore.

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
| broadcast path of the content guide manifest | the content guide endpoint: `?sid&start&end`, `?sid&now_next=true` (Annex D.6.2; Phase 5) |
| any other broadcast path | manifests, segments and other files from the file store |
| `/dvbi/slep.xml` | the DVB-I service list entry points, which the broadcast identifies only by URN (`urn:dvb:metadata:nativeip:dvb-i-slep`, 8.2.5.1), so the gateway chooses the URL (8.1.7.3); with several networks, one document offering every network's lists |
| `/time` | the NIP wall clock (8.6.3) |
| `/api/status`, `/api/scan` | admin only: sources, tuners, sessions, store usage; start a rescan. The player never uses these. |

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

When the broadcast has no DVB-I list (TP37, 1067000.ts; not conformant),
NippApp generates an entry points file and a list from the SIF and the
database, declaring a content guide URL so the service guide has an endpoint.

## Request handling and on-demand tuning

As 8.5.3 sets out:

1. Match the request path (ignoring an optional `?serviceId=`) against the SIF.
   No match: 404.
2. Tune the carrier the SIF and NIF give, if not already tuned. This applies to
   any URL in the SIF, not only service manifests: lists, guide files and other
   files can be on other carriers (8.1.7.5, 9.5.4).
3. Serve from the file store. A file not yet received: wait a set time, then 404.
   No tuner free: 503 with Retry-After.

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
`instance.deliveryParameters` (`type` `dash` with `dash.mpdUrl`, or `hls` with
`other.url`).

## Discovery (Phase B)

DNS-SD/mDNS is mandatory for a DM3 gateway (11.1): service type
`_dvbservdsc._tcp` (and `_http._tcp`), a TXT record with
`dvbi_sep=<entry points URL on the gateway's IP>`, optionally `manuf`,
`model`, `tuners` and `orb_pos_A`. Avahi on Linux. Browser clients cannot use
mDNS; they load the player from the gateway.

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
captures use FLUTE with Content-Encoding null. (EXT_TIME is now decoded; see
Clock.)

## Open points

- A PCAP has no tuning information, so its sources entry is made up. The
  NACI gives its identity (SES: network 1, carrier 1045), but there is no
  check yet that the NIF's parameters for that carrier match the entry.
- DVBCore has one LCN space, so lists that ask for the same LCNs clash: with
  Inverto and SES searched together, SES's B1 and B2 get 1003 and 1004. The
  registry (milestone 4) serves each broadcast list with its own LCNs, so
  clients are not affected.
- The MPE carrier service (DVB-NIP-VV) is itself listed as a channel, at LCN
  1000. It should probably be hidden.

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

- A file that is neither in the store nor the registry is waited for, then
  404, even when no SIF lists its path. Answering 404 at once for paths the SIF
  doesn't list (8.5.3) needs a lookup the NIP library doesn't offer yet.
- One NIF: the NIP library keeps the last one received. It only adds
  transports to the database, so nothing is lost when it is replaced.
- SES's "DVB-NIP Offline Content" list (`/ses.com/private/dvbi/...`) is in its
  SIF and entry points but doesn't arrive within the 8 s wait, so it answers
  404.
- After a tuner is released, the first request for a service's manifest is
  answered at once with the stored copy while the carrier is tuned again. Not
  yet checked in a browser.
- VandVD1 (DVB-HB) and OTT instances can't be played through the gateway.
- Entry point query filtering, and the move to civetweb.

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

Still to do for milestone 5: two carriers at once with captures that share a
clock, and a full AddressSanitizer pass over all of it.

## Resume here (state at 2026-10-14)

Milestones 1-4 and milestone 5's tuner contention and metadata change are
committed (NippApp 3fc6016, DVBCore 9fb3cb4).

Not committed yet (several networks and lists): in DVBCore
`dvbnip/src/nip_mgr.c`, `dvbnip/src/nip_misc.c`, `dvbnip/src/nip_misc.h`; in
NippApp `Makefile`, `src/registry.c`, `player/player.html` and this file. `dvbnip/test/DVB-NIP-DVB-Iplan.md` and `dvbnip/test/DTVkit.code-workspace`
are local files, not to be committed.

To try Inverto + Eutelsat, a sources file with absolute paths, Inverto first:

```
11471 V 27500 ts /home/jason/DTVkit/Streams/DvbNIP/1097000.ts loop
11013 H 27500 ts /home/jason/DTVkit/Streams/DvbNIP/1087000.ts loop
```

then `./build/nippapp -d <dir> -s <file> --scan` and open
`http://<ip>:8080/player/`. Add
`11141 H 23500 pcap /home/jason/DTVkit/Streams/DvbNIP/2025-02-11_1min.pcap loop`
for a third carrier, to see tuner contention.

Next: the rest of milestone 5.
