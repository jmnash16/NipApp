#!/bin/bash
#
# Runs NippApp built with AddressSanitizer through the test scenarios on the captures, checking the
# results and that every run ends with no AddressSanitizer error or leak. Rebuilds the normal binary
# at the end.
#
#   tools/asan-check.sh            all scenarios
#   tools/asan-check.sh scans      one scenario: scans, networks, change, carousels, nolist, retune
#                                  or prebuffer
#
# CAPTURES (default ../Streams/DvbNIP next to the NippApp directory) is where the captures are, PORT
# (default 18080) the HTTP port used, and KEEP=1 keeps the working directories and logs.
#
# Takes about 15 minutes: captures are fed in real time.

set -uo pipefail

nippapp_dir="$(cd "$(dirname "$0")/.." && pwd)"
captures="${CAPTURES:-$nippapp_dir/../Streams/DvbNIP}"
port="${PORT:-18080}"
base="http://localhost:$port"
work="$(mktemp -d /tmp/nippapp-asan.XXXXXX)"
pid=""
failures=0
checks=0

cd "$nippapp_dir" || exit 1

finish()
{
   [ -n "$pid" ] && kill -INT "$pid" 2>/dev/null
   echo
   echo "Rebuilding the normal binary"
   make clean > /dev/null && make > /dev/null 2>&1 || echo "  normal build failed"
   if [ "${KEEP:-0}" = "1" ]; then
      echo "Working directories and logs kept in $work"
   else
      rm -rf "$work"
   fi
}
trap finish EXIT

pass() { checks=$((checks + 1)); echo "  ok    $1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); echo "  FAIL  $1"; }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }

# capture freq pol symrate kind file
source_line()
{
   echo "$1 $2 $3 $4 $captures/$5 loop"
}

# start <dir> <sources file> <log> [--scan]: starts NippApp and waits for its HTTP server
start()
{
   local dir="$1" sources="$2" log="$3"
   shift 3
   mkdir -p "$dir"
   ./build/nippapp -d "$dir" -s "$sources" -p "$port" "$@" > "$log" 2>&1 &
   pid=$!
   for _ in $(seq 1 400); do
      grep -q "HTTP server" "$log" 2>/dev/null && return 0
      kill -0 "$pid" 2>/dev/null || break
      sleep 1
   done
   fail "NippApp started ($log)"
   return 1
}

# stop <log>: stops NippApp with SIGINT, and checks it stopped cleanly with no AddressSanitizer report
stop()
{
   local log="$1"
   kill -INT "$pid" 2>/dev/null
   for _ in $(seq 1 60); do
      kill -0 "$pid" 2>/dev/null || break
      sleep 1
   done
   if kill -0 "$pid" 2>/dev/null; then
      kill -KILL "$pid" 2>/dev/null
      fail "stopped within 60 s ($(basename "$log"))"
   else
      check "stopped cleanly, no AddressSanitizer report ($(basename "$log"))" \
         "grep -q 'NippApp: stopped' '$log' && ! grep -q 'AddressSanitizer' '$log'"
   fi
   pid=""
}

status() { curl -s -o /dev/null -m "${2:-20}" -w '%{http_code}' "$base$1"; }

channels() { grep -c '^  LCN' "$1"; }

# every channel line from a search, for the checks on names
has_channel() { grep -q "^  LCN *[0-9]*  $2  *onid" "$1"; }

# ---------------------------------------------------------------------------------------------------
# Each capture searched on its own

scenario_scans()
{
   local name freq pol sr kind file min dir log
   echo "Searches, one capture each"
   while read -r name freq pol sr kind file min; do
      if [ ! -f "$captures/$file" ]; then
         echo "  skip  $name: $file not found"
         continue
      fi
      dir="$work/scan-$name"
      log="$work/scan-$name.log"
      source_line "$freq" "$pol" "$sr" "$kind" "$file" > "$work/scan-$name.conf"
      start "$dir" "$work/scan-$name.conf" "$log" --scan || { stop "$log"; continue; }
      check "$name: at least $min channels ($(channels "$log") found)" "[ $(channels "$log") -ge $min ]"
      stop "$log"
   done <<EOF
inverto  11471 V 27500 ts   1097000.ts                2
eutelsat 11013 H 27500 ts   1087000.ts                5
hbtxp6   11013 H 27500 ts   20240902Hbtxp6_1mn.ts     5
ses      11141 H 23500 pcap 2025-02-11_1min.pcap      6
tp37     11023 H 23500 pcap TP37_2min_231106.pcap     3
EOF
}

# ---------------------------------------------------------------------------------------------------
# Inverto, Eutelsat and SES together: merged entry points, names, tuner contention, restart, DVB-HB

scenario_networks()
{
   local dir="$work/networks" conf="$work/networks.conf" log offerings code
   echo "Three networks on two tuners"
   {
      source_line 11471 V 27500 ts 1097000.ts
      source_line 11013 H 27500 ts 1087000.ts
      source_line 11141 H 23500 pcap 2025-02-11_1min.pcap
   } > "$conf"

   log="$work/networks-scan.log"
   start "$dir" "$conf" "$log" --scan || { stop "$log"; return; }
   check "SES's B1 keeps its name" "has_channel '$log' B1"
   check "Eutelsat's VandVA1 keeps its name" "has_channel '$log' VandVA1"
   check "the carrier services (DVB-NIP-VV) are hidden" "! has_channel '$log' DVB-NIP-VV"
   check "three SIFs kept" "[ -f '$dir/disks/nip/sif2.xml' ]"
   check "entry points of three networks kept" "[ \$(ls '$dir/registry/slep' | wc -l) -eq 3 ]"
   offerings=$(curl -s -m 10 "$base/dvbi/slep.xml" | grep -o '<ProviderOffering' | wc -l)
   check "/dvbi/slep.xml offers three providers ($offerings)" "[ $offerings -ge 3 ]"

   # both tuners busy: a third carrier gets 503
   check "Inverto's A2 tunes" "[ \$(status /inverto.tv/a2/livesim2/tsbd_8/A2/Manifest.mpd) = 200 ]"
   check "Eutelsat's A1 tunes" "[ \$(status /livesim2/mup_1/A1/Manifest.mpd) = 200 ]"
   code=$(status /ses.com/livesim2/B1/video/999999.m4s)
   check "SES gets 503 with both tuners in use ($code)" "[ $code = 503 ]"
   code=$(status "/dvb-hb/318_601_17702.mpd/?src=DVB-HB&pos=13&freq=11317")
   check "DVB-HB request gets 501 ($code)" "[ $code = 501 ]"
   check "entry points filtered by provider (ProviderName[]=SES)" \
      "[ \$(curl -s -m 10 '$base/dvbi/slep.xml?ProviderName%5B%5D=SES' | grep -o '<ProviderOffering' | wc -l) -eq 1 ]"
   check "entry points by country: only lists that declare none (TargetCountry=GBR: Inverto's)" \
      "[ \$(curl -s -m 10 '$base/dvbi/slep.xml?TargetCountry=GBR' | grep -o '<ProviderOffering' | wc -l) -eq 1 ]"
   check "entry points: an undefined parameter is refused (400)" "[ \$(status '/dvbi/slep.xml?Unknown=1') = 400 ]"
   check "entry points: a Delivery value not in the table is refused (Delivery=dash: 400)" \
      "[ \$(status '/dvbi/slep.xml?Delivery=dash') = 400 ]"
   if command -v avahi-browse > /dev/null && grep -q "mDNS: announced" "$log"; then
      check "announced with mDNS: _dvbservdsc._tcp with dvbi_sep on port $port" \
         "timeout 10 avahi-browse -rpt _dvbservdsc._tcp | grep -q 'dvbi_sep=http://[0-9.]*:$port/dvbi/slep.xml'"
   fi
   check "/api/status is JSON with three sources" \
      "curl -s -m 10 '$base/api/status' | python3 -I -c 'import json,sys; sys.exit(len(json.load(sys.stdin)[\"sources\"]) != 3)'"
   code=$(curl -s -o /dev/null -m 10 -w '%{http_code}' -X POST "$base/api/scan")
   check "/api/scan starts a scan ($code)" "[ $code = 202 ]"
   code=$(curl -s -o /dev/null -m 10 -w '%{http_code}' -X POST "$base/api/scan")
   check "/api/scan while one runs is refused ($code)" "[ $code = 409 ]"
   for _ in $(seq 1 200); do
      curl -s -m 10 "$base/api/status" | grep -q '"running":false' && break
      sleep 2
   done
   check "the scan asked for completes" "curl -s -m 10 '$base/api/status' | grep -q '\"running\":false,\"lastCompleted\":true'"
   code=$(curl -s -o /dev/null -m 20 -w '%{http_code} %{time_total}' "$base/materials/DVB-NIP_EW.jpg")
   check "a path no SIF lists gets 404 at once ($code s)" "[ '${code% *}' = 404 ] && awk 'BEGIN { exit !(${code#* } < 1) }'"
   stop "$log"

   # restart without a search: everything from disk, each B1 on its own carrier
   log="$work/networks-restart.log"
   start "$dir" "$conf" "$log" || { stop "$log"; return; }
   check "SES's B1 tunes after a restart" "[ \$(status /ses.com/livesim2/B1/Manifest.mpd) = 200 ]"
   check "Eutelsat's B1 tunes after a restart" "[ \$(status /livesim2/B1/Manifest.mpd) = 200 ]"
   check "the SES carrier was tuned" "grep -q '2025-02-11_1min.pcap (PCAP' '$log'"
   check "the Eutelsat carrier was tuned" "grep -q '1087000.ts (TS)' '$log'"
   stop "$log"
}

# ---------------------------------------------------------------------------------------------------
# A metadata version change: Eutelsat's carrier 601 in April 2024, then September 2024

scenario_change()
{
   local dir="$work/change" log
   echo "Metadata version change (Eutelsat April to September 2024)"
   if [ ! -f "$captures/20240902Hbtxp6_1mn.ts" ]; then
      echo "  skip  20240902Hbtxp6_1mn.ts not found"
      return
   fi
   source_line 11013 H 27500 ts 1087000.ts > "$work/change-april.conf"
   source_line 11013 H 27500 ts 20240902Hbtxp6_1mn.ts > "$work/change-september.conf"

   log="$work/change-april.log"
   start "$dir" "$work/change-april.conf" "$log" --scan || { stop "$log"; return; }
   stop "$log"

   log="$work/change-september.log"
   start "$dir" "$work/change-september.conf" "$log" || { stop "$log"; return; }
   status /livesim2/mup_1/A1/Manifest.mpd > /dev/null
   for _ in $(seq 1 60); do
      grep -q "registry: kept dvb_i_sl_physical.xml" "$log" && break
      sleep 1
   done
   sleep 10
   check "April's A1 is gone (404)" "[ \$(status /livesim2/mup_1/A1/Manifest.mpd) = 404 ]"
   check "September's A1 plays" "[ \$(status /eutelsat/livesim2/mup_1/A1/Manifest.mpd) = 200 ]"
   check "the entry points are September's" "curl -s -m 10 '$base/dvbi/slep.xml' | grep -q eutelsat/materials"
   stop "$log"
   check "the database has September's channels" \
      "command -v sqlite3 > /dev/null && [ \$(sqlite3 '$dir/dtvkit.sqlite3' .dump | grep -a -c '/eutelsat/livesim2') -ge 3 ]"
}

# ---------------------------------------------------------------------------------------------------
# SES's object carousels: its offline content list, a cover whose name is percent-encoded, and the
# content guide schedule, sent gzip

# fetch <path> until it arrives (a carousel can take a while to come round), up to <seconds>
fetch()
{
   local path="$1" seconds="$2" out="$3" start=$SECONDS
   while [ $((SECONDS - start)) -lt "$seconds" ]; do
      [ "$(curl -s -o "$out" -m 20 -w '%{http_code}' "$base$path")" = 200 ] && return 0
   done
   return 1
}

scenario_carousels()
{
   local dir="$work/carousels" conf="$work/carousels.conf" log="$work/carousels.log" size
   echo "Object carousels (SES)"
   source_line 11141 H 23500 pcap 2025-02-11_1min.pcap > "$conf"
   start "$dir" "$conf" "$log" --scan || { stop "$log"; return; }
   check "the offline content list arrives" \
      "fetch /ses.com/private/dvbi/service_list_ses_private.xml 60 '$work/private.xml' && grep -q '<ServiceList' '$work/private.xml'"
   check "the registry keeps it" "[ -f '$dir/registry/ses.com/private/dvbi/service_list_ses_private.xml' ]"
   check "a percent-encoded name is found (P.M.%20Magazin.jpg)" \
      "fetch '/ses.com/private/pdf/covers/P.M.%20Magazin.jpg' 60 '$work/cover.jpg'"
   check "the content guide schedule arrives" \
      "fetch /ses.com/dvbi/cg/tva_schedule_20250210230000_P28DT20M_CoreSet.xml 90 '$work/schedule.xml'"
   size=$(stat -c %s "$work/schedule.xml" 2>/dev/null || echo 0)
   check "the schedule is served decoded ($size bytes)" "[ $size -gt 900000 ] && head -c 40 '$work/schedule.xml' | grep -q '<?xml'"

   # a carousel's file that hasn't come round (this one is only partly in the capture): try again
   check "a carousel file still on its way is 503 with Retry-After (brayden-prato.jpg)" \
      "curl -s -D - -o /dev/null -m 20 '$base/ses.com/private/pictures/brayden-prato.jpg' | grep -q '^Retry-After'"

   # the content guide endpoint (Annex D.6.2), at the manifest's URL
   local cg="/ses.com/dvbi/cg/manifest.xml?sid=tag:ses.com,2024:B1.1"
   check "guide: now and next for B1 (two events)" \
      "fetch '$cg&now_next=true' 90 '$work/nownext.xml' && [ \$(grep -o '<ScheduleEvent' '$work/nownext.xml' | wc -l) -eq 2 ]"
   check "guide: B1 on 11 February 2025, 18:00-24:00 (seven events)" \
      "fetch '$cg&start=1739296800&end=1739318400' 30 '$work/window.xml' && [ \$(grep -o '<ScheduleEvent' '$work/window.xml' | wc -l) -eq 7 ]"
   check "guide: a query without an end is refused (400)" "[ \$(status '$cg&start=1739296800') = 400 ]"
   check "guide: a window not on whole 3 hours is refused (400)" "[ \$(status '$cg&start=1739296801&end=1739318401') = 400 ]"
   check "guide: now_next=window gives more than now and next" \
      "fetch '$cg&now_next=window' 30 '$work/window2.xml' && [ \$(grep -o '<ScheduleEvent' '$work/window2.xml' | wc -l) -gt 2 ]"
   check "guide: an unknown service has empty tables" \
      "fetch '/ses.com/dvbi/cg/manifest.xml?sid=unknown&now_next=true' 30 '$work/unknown.xml' && grep -q '<ProgramLocationTable/>' '$work/unknown.xml'"
   stop "$log"
}

# ---------------------------------------------------------------------------------------------------
# TP37, a broadcast without DVB-I entry points or a list: the gateway's own list offers its services

scenario_nolist()
{
   local dir="$work/nolist" conf="$work/nolist.conf" log="$work/nolist.log" code
   echo "A broadcast without a DVB-I list (TP37)"
   source_line 11023 H 23500 pcap TP37_2min_231106.pcap > "$conf"
   start "$dir" "$conf" "$log" --scan || { stop "$log"; return; }
   check "the entry points offer the gateway's list" \
      "curl -s -m 10 '$base/dvbi/slep.xml' | grep -q 'nippapp_list.xml'"
   check "the gateway's list has TP37's three services" \
      "[ \$(curl -s -m 10 '$base/dvbi/nippapp_list.xml' | grep -o '<Service ' | wc -l) -eq 3 ]"
   for path in /inverto.tv/s0/live.m3u8 /inverto.tv/s1/crits_linear.mpd /10.0.1.229/livesim/B1/Manifest.mpd; do
      code=$(status "$path")
      check "$path from the gateway's list plays ($code)" "[ $code = 200 ]"
   done
   stop "$log"
}

# ---------------------------------------------------------------------------------------------------
# A re-tune: an HLS media playlist kept from before is not served; the fresh one lists segments
# near the broadcast clock (SES, 2 s segments numbered by the UTC second / 2)

scenario_retune()
{
   local dir="$work/retune" conf="$work/retune.conf" log="$work/retune.log" last now released
   echo "Re-tune (SES)"
   source_line 11141 H 23500 pcap 2025-02-11_1min.pcap > "$conf"
   start "$dir" "$conf" "$log" --scan || { stop "$log"; return; }
   released=$(grep -c "tuner 0: stopped" "$log")
   check "the HLS media playlist plays" "[ \$(status /ses.com/livesim2/B1/playlist-SD.m3u8) = 200 ]"
   for _ in $(seq 1 150); do
      [ "$(grep -c "tuner 0: stopped" "$log")" -gt "$released" ] && break
      sleep 1
   done
   sleep 3
   curl -s -m 20 -o "$work/retune.m3u8" "$base/ses.com/livesim2/B1/playlist-SD.m3u8"
   last=$(grep -o '[0-9]*\.m4s' "$work/retune.m3u8" | tail -1 | cut -d. -f1)
   now=$(date -u -d "$(curl -s -m 5 "$base/time")" +%s 2>/dev/null || echo 0)
   check "after a re-tune the playlist is fresh (last segment $((${last:-0} * 2 - now)) s from the clock)" \
      "[ -n '$last' ] && [ \$(( ${last:-0} * 2 - $now )) -le 20 ] && [ \$(( $now - ${last:-0} * 2 )) -le 20 ]"
   stop "$log"
}

# ---------------------------------------------------------------------------------------------------
# Pre-buffering (SES): while a client receives B1, the other channels on the carrier are received
# in B1's format, so a change to B2 finds its latest segments at once; they stop with B1

# the services being received, as "path:pre" or "path:client" lines
receiving()
{
   curl -s -m 10 "$base/api/status" | python3 -c '
import json, sys
for s in json.load(sys.stdin)["streams"]:
    for r in s["receiving"]:
        print(r["path"] + (":pre" if r["prebuffered"] else ":client"))'
}

scenario_prebuffer()
{
   local dir="$work/prebuffer" conf="$work/prebuffer.conf" log="$work/prebuffer.log" b="/ses.com/livesim2"
   local watcher now n took released stopped
   echo "Pre-buffering (SES)"
   source_line 11141 H 23500 pcap 2025-02-11_1min.pcap > "$conf"
   check "--prebuffer refuses a value that isn't all, off or a number" \
      "! ./build/nippapp -d '$dir' -s '$conf' --prebuffer some > /dev/null 2>&1"
   start "$dir" "$conf" "$log" --scan || { stop "$log"; return; }

   # a player on B1: the manifest every second
   ( while true; do curl -s -o /dev/null -m 5 "$base$b/B1/Manifest.mpd"; sleep 1; done ) &
   watcher=$!
   sleep 15
   receiving > "$work/prebuffer.receiving"
   check "B1 is received for the client" "grep -qx '$b/B1/Manifest.mpd:client' '$work/prebuffer.receiving'"
   check "B2 and C1 are pre-buffered, as DASH" \
      "grep -qx '$b/B2/Manifest.mpd:pre' '$work/prebuffer.receiving' && grep -qx '$b/C1/Manifest.mpd:pre' '$work/prebuffer.receiving'"
   check "no HLS service is pre-buffered" "! grep -q 'm3u8:pre' '$work/prebuffer.receiving'"
   check "/api/status gives the setting" \
      "curl -s -m 10 '$base/api/status' | grep -q '\"prebuffer\":{\"enabled\":true,\"maxPerCarrier\":null}'"

   took=$(curl -s -o /dev/null -m 20 -w '%{http_code} %{time_total}' "$base$b/B2/Manifest.mpd")
   check "a change to B2: its manifest at once ($took s)" "[ '${took% *}' = 200 ] && awk 'BEGIN { exit !(${took#* } < 0.5) }'"
   now=$(date -u -d "$(curl -s -m 5 "$base/time")" +%s 2>/dev/null || echo 0)
   n=$(( (now - 6) / 2 ))
   took=$(curl -s -o /dev/null -m 20 -w '%{http_code} %{time_total}' "$base$b/B2/HD/$n.m4s")
   check "a change to B2: the segment 6 s behind live at once ($took s)" \
      "[ '${took% *}' = 200 ] && awk 'BEGIN { exit !(${took#* } < 0.5) }'"
   check "B2 is then the client's" "receiving | grep -qx '$b/B2/Manifest.mpd:client'"

   stopped=$(grep -c "tuner 0: stopped" "$log")
   kill "$watcher" 2>/dev/null
   wait "$watcher" 2>/dev/null
   released=0
   for _ in $(seq 1 90); do
      [ "$(grep -c "tuner 0: stopped" "$log")" -gt "$stopped" ] && { released=1; break; }
      sleep 1
   done
   check "with no client, the pre-buffered services stop and the tuner is released" \
      "[ $released = 1 ] && [ -z \"\$(receiving)\" ]"
   stop "$log"

   log="$work/prebuffer-1.log"
   start "$dir" "$conf" "$log" --prebuffer 1 || { stop "$log"; return; }
   status "$b/B1/Manifest.mpd" > /dev/null
   sleep 5
   check "--prebuffer 1: one channel pre-buffered" "[ \$(receiving | grep -c ':pre$') -eq 1 ]"
   stop "$log"
}

# ---------------------------------------------------------------------------------------------------

if curl -s -o /dev/null -m 2 "$base/time"; then
   echo "Port $port is in use; set PORT" >&2
   exit 1
fi
if [ ! -d "$captures" ]; then
   echo "No captures at $captures; set CAPTURES" >&2
   exit 1
fi

echo "Building NippApp with AddressSanitizer"
make clean > /dev/null && make ASAN=1 > "$work/build.log" 2>&1 || { echo "build failed, see $work/build.log"; KEEP=1; exit 1; }

scenarios="${1:-scans networks change carousels nolist retune prebuffer}"
for scenario in $scenarios; do
   case "$scenario" in
      scans|networks|change|carousels|nolist|retune|prebuffer) "scenario_$scenario" ;;
      *) echo "unknown scenario $scenario" >&2; failures=$((failures + 1)) ;;
   esac
done

echo
echo "$((checks - failures)) of $checks checks passed"
[ $failures -eq 0 ]
