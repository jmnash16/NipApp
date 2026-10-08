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
PCAP input therefore wraps each IP packet in a GSE packet and passes it to the
callback DVBCore registers with `STB_TuneSetGSEDataCallback`. That runs the
real GSE search (`APP_SI_MODE_GSE_NO_SI`, `ManageDVBNIPGSE`) with no changes
to DVBCore.

A sources file maps tuning parameters to files. Tuning a frequency that has no
entry reports no lock, as an empty transponder would. The captures live in
`DTVkit/Streams/DvbNIP`.

```
# freq(MHz) pol  symrate  kind  file
11355       H    27500    ts    ../Streams/DvbNIP/1067000.ts
11471       V    27500    ts    ../Streams/DvbNIP/1097000.ts
10900       H    30000    pcap  ../Streams/DvbNIP/2025-02-11_1min.pcap
```

Input is paced in real time: TS by PCR or TDT, PCAP by packet timestamps.
When a file ends, its source either loops or stops, set per entry; stopping
matches the test tool's `-w` hold.

Every stream carries NIP Actual Carrier Information (TSI 1 on the announcement
address, LCT extension 68, every 250 ms; 8.7). NippApp reads it to confirm
which carrier is tuned; for PCAP input it is the only source of the carrier's
identity.

## Structure

```
NippApp/
  Makefile               builds against DVBCore's libdvbcore.a
  DESIGN.md
  sources.conf           example sources file
  src/
    main.c               options, startup, main loop, shutdown
    events.c             DVBCore event handler (search progress, NIP ready, updates)
    search.c             first-run setup (satellite/LNB records) and scans
    registry.c           local service list registry: entry points, lists, query filtering
    guide.c              content guide endpoint (Phase 5)
    httpd.c              HTTP server: player, registry, media, /time, admin/status
    mdns.c               DNS-SD/mDNS announcement (Phase B)
  platform/
    os/                  copy of App/platform/os/linux (tasks, mutexes, queues, clock)
    hw/
      stbhwtun.c         file tuner: sources file, lock, TS feeder, PCAP→GSE feeder
      stbhwdmx.c         section/PID filtering, trimmed from App's (no video/audio decoding)
      stbhwmem.c, stbhwdsk.c, stbhwnvm.c, stbhwcrypt.c, stbhwsp.c, stbhwini.c
                         taken from App where they have no UI dependencies
      stubs.c            empty video/audio, OSD, front panel, PVR, upgrade, media player
  player/
    player.html          new web player (dvbi-client-js for metadata; dash.js + hls.js)
    vendor/              built dist/ of dvbi-client-js
  platform/COPIED.md     files copied from App, with their source and changes
  third_party/
    mongoose.c/.h        HTTP library (to be replaced by a current mongoose or civetweb)
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
   links with `libdvbcore.a`, `sqlite3`, `xml2` and `pthread`. No SDL, ffmpeg,
   Lua or Qt.
3. The build defines `DVBNIP_BUILD`, the same as DVBCore.

## Startup and scanning

1. `APP_InitialiseDVB(EventHandler, DVB_INIT_NO_TELETEXT_OR_SUBTITLES)`.
2. On first run (empty database), create a satellite and LNB record.
3. If the database has no NIP services, or `--scan` is given:
   `ASI_SetSearchServiceType(SEARCH_SERVICE_TYPE_DVBNIP | ..._DVBNIP_GSE)`, then
   `ACTL_StartManualSearch(SIGNAL_QPSK, params, ...)` for each sources entry,
   one at a time, with `is_gse` set for PCAP entries. The event handler
   advances to the next entry when each search completes. The sources file
   stands in for the preset bootstrap stream list of Annex C.4.1.
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
| `/time` | the NIP wall clock (8.6.3) |
| `/api/status`, `/api/scan` | admin only: sources, tuners, sessions, store usage; start a rescan. The player never uses these. |

The gateway's address in rewritten URLs is a literal IP address, not a `.local`
name: `dvbi-client-js` allows plain HTTP only to private IP addresses and
localhost.

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
file tuner offers more than one tuner path (e.g. 2), so two carriers can be
served at once.

Served MPDs list only segments actually available (9.1.2):
`minimumUpdatePeriod` 2 s, the first MPD held until the client's live point
lies after the join, and the AAC config fix from the test tool.

## Clock

The NIP wall clock is the NTP time in the EXT_TIME LCT extension (type 2) of
FLUTE/ROUTE packets; the bootstrap stream's announcement channel is the
reference (8.6.1). NippApp serves it at `/time` as xs:dateTime, with the
`iso`, `xsdate` and `ms` query forms (8.6.3), and inserts or replaces
`UTCTiming` in served MPDs with `urn:mpeg:dash:utc:http-xsdate:2014` pointing
at it (9.1.2). This is one path for MPE and GSE. TDT/TOT stays as a fallback
for older captures (TP37 carries EXT_TIME on only 3 of 11 sessions).
`flute.c` skips this extension today, so DVBCore needs a change to expose it.

## Player

`player/player.html` uses `dvbi-client-js` (`/home/jason/dvbi-client-js`, a
pure JavaScript DVB-I client) for entry points, lists, LCNs, regions and the
guide, and dash.js and hls.js for playback. NippApp serves the page and the
client's Web Worker from its own origin and vendors the client's built `dist/`
(Node 20.19+ to build).

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
| 4 | HTTP, registry, player | the player starts from the broadcast entry points served by NippApp, loads a broadcast list and plays DASH and HLS; tuning happens on request and is released when idle |
| 5 | Robustness | two carriers at once, tuner contention, metadata version change updates the list, AddressSanitizer clean |
| B | DVB-I endpoint | entry point queries, mDNS, content guide (service guide and content guide), works with an independent DVB-I client |

## Not yet supported by DVBCore

Mandatory for a DM3 gateway, none implemented yet: ROUTE reception, compressed
signalling files (FDT Content-Encoding), 48-bit TOIs (`flute.c` keeps 32 bits),
EXT_TIME and mDNS. For GSE only: ROHC and GSE-Lite link-layer signalling. All
our captures use FLUTE with Content-Encoding null.

## Open points

- `stbhwdmx.c` from App is 2,500 lines and mixes section filtering with
  video/audio PES handling; how much to trim will be clear in milestone 1.
- The PCAP has no tuning information. Its tuning parameters come from the
  sources file; the NIF in the SES capture names network 1, carrier 1045, and
  its NACI confirms which carrier it is.
- A TS search through the DVBCore search engine expects SI (PAT/PMT/SDT/NIT).
  `1097000.ts` has PAT, SDT, PMT and TDT/TOT but no NIT, so milestone 2 will
  show whether the search accepts that.
