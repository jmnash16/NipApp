#!/usr/bin/env python3
#
# Makes a test capture with a large file in an object carousel, complete, from a capture that has
# the carousel: the carousel session's packets are replaced by generated FLUTE packets carrying the
# file. The recordings hold only part of each large carousel file (SES's videos are 17-291 MB, the
# capture 64 s), so the disk store can't be tested with them as they are.
#
#   tools/make-carousel-pcap.py in.pcap out.pcap [options]
#
# By default, SES's video carousel (224.0.49.5:49005, TSI 49005) gets one 30 MB file at the path of
# HDplus_ExtraScreen.mp4, which SES's offline content list offers. The file is sent twice in the
# capture's time: the first copy misses every 37th packet, the second is whole, so a gateway that
# joins during the first copy has to fill it in from the second (as from a carousel's next pass),
# and one that joins during the second fills in the start when the capture loops. An FDT instance
# announcing the file is sent every half second. The other packets, the covers' session sharing the
# address included, are kept as they are.
#
# Prints the file's MD5; --content-out writes the file itself, to compare with what is served.
# Needs only Python 3.9 or later.

import argparse
import base64
import hashlib
import heapq
import random
import struct
import sys

SYMBOL_LEN = 1024          # encoding symbol length, as SES uses
BLOCK_LEN = 512            # maximum source block length (symbols)
FDT_INTERVAL = 0.5


def read_pcap(path):
    """The global header, and each record as (time, header fields, frame)"""
    f = open(path, 'rb')
    header = f.read(24)
    magic = struct.unpack('<I', header[:4])[0]
    if magic in (0xa1b2c3d4, 0xa1b23c4d):
        endian = '<'
    elif magic in (0xd4c3b2a1, 0x4d3cb2a1):
        endian = '>'
    else:
        sys.exit('%s is not a classic PCAP file' % path)
    nano = magic in (0xa1b23c4d, 0x4d3cb2a1)
    link = struct.unpack(endian + 'I', header[20:24])[0]

    def records():
        while True:
            rec = f.read(16)
            if len(rec) < 16:
                return
            sec, frac, caplen, origlen = struct.unpack(endian + 'IIII', rec)
            frame = f.read(caplen)
            yield sec + frac / (1e9 if nano else 1e6), frame

    return header, endian, nano, link, records()


def ip_offset(link, frame):
    if link == 1:                      # Ethernet, maybe with a VLAN tag
        return 18 if struct.unpack('>H', frame[12:14])[0] == 0x8100 else 14
    if link == 113:                    # Linux cooked
        return 16
    return 0                           # raw IP


def session_of(link, frame):
    """(destination address, port, TSI) of an IPv4 UDP LCT packet, or None"""
    off = ip_offset(link, frame)
    ip = frame[off:]
    if len(ip) < 28 or ip[0] >> 4 != 4 or ip[9] != 17:
        return None
    ihl = (ip[0] & 15) * 4
    dst = '.'.join(str(b) for b in ip[16:20])
    port = struct.unpack('>H', ip[ihl + 2:ihl + 4])[0]
    lct = ip[ihl + 8:]
    if len(lct) < 8 or lct[0] >> 4 != 1:
        return None
    flags = lct[1]
    o = 4 + 4 * (((lct[0] >> 2) & 3) + 1)
    tsi_len = 4 * (flags >> 7 & 1) + 2 * (flags >> 4 & 1)
    tsi = int.from_bytes(lct[o:o + tsi_len], 'big') if tsi_len else 0
    return dst, port, tsi


def lct_packet(tsi, toi, payload_id, payload, transfer_len=None, fdt_id=None):
    """An LCT packet as SES sends them: 16-bit TSI and TOI, EXT_FDT for an FDT, EXT_FTI"""
    ext = b''
    if fdt_id is not None:
        ext += bytes([0xc0, (2 << 4) | (fdt_id >> 16 & 0x0f)]) + struct.pack('>H', fdt_id & 0xffff)
    if transfer_len is not None:
        ext += (bytes([0x40, 4]) + transfer_len.to_bytes(6, 'big') + b'\0\0' +
                struct.pack('>HI', SYMBOL_LEN, BLOCK_LEN))
    header_len = 12 + len(ext)
    header = bytes([0x10, 0x10, header_len // 4, 0]) + b'\0\0\0\0' + struct.pack('>HH', tsi, toi)
    return header + ext + struct.pack('>I', payload_id) + payload


def frame_for(template, link, payload):
    """The template packet's framing and addresses with another UDP payload"""
    off = ip_offset(link, template)
    ip = bytearray(template[off:off + 20])
    ihl = (template[off] & 15) * 4
    udp = bytearray(template[off + ihl:off + ihl + 8])
    total = 20 + 8 + len(payload)
    ip[0] = 0x45
    struct.pack_into('>H', ip, 2, total)
    struct.pack_into('>H', ip, 10, 0)
    words = struct.unpack('>10H', bytes(ip))
    s = sum(words)
    s = (s & 0xffff) + (s >> 16)
    s = (s & 0xffff) + (s >> 16)
    struct.pack_into('>H', ip, 10, ~s & 0xffff)
    struct.pack_into('>H', udp, 4, 8 + len(payload))
    struct.pack_into('>H', udp, 6, 0)
    return bytes(template[:off]) + bytes(ip) + bytes(udp) + payload


def generated(args, content, start, end, tsi):
    """The new session's packets, in time order: (time, UDP payload)"""
    md5 = base64.b64encode(hashlib.md5(content).digest()).decode()
    fdt = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           '<FDT-Instance xmlns="urn:IETF:metadata:2005:FLUTE:FDT" Expires="4294967295">\n'
           '  <File Content-Location="%s" TOI="%d" Content-Length="%d" Transfer-Length="%d" '
           'Content-Type="%s" Content-Encoding="null" Content-MD5="%s" FEC-OTI-FEC-Encoding-ID="0" '
           'FEC-OTI-Maximum-Source-Block-Length="%d" FEC-OTI-Encoding-Symbol-Length="%d"/>\n'
           '</FDT-Instance>\n' % (args.location, args.toi, len(content), len(content), args.type, md5,
                                  BLOCK_LEN, SYMBOL_LEN)).encode()
    fdt_packets = [lct_packet(tsi, 0, (i // BLOCK_LEN) << 16 | (i % BLOCK_LEN),
                              fdt[i * SYMBOL_LEN:(i + 1) * SYMBOL_LEN], len(fdt), 1)
                   for i in range((len(fdt) + SYMBOL_LEN - 1) // SYMBOL_LEN)]

    symbols = (len(content) + SYMBOL_LEN - 1) // SYMBOL_LEN

    def data(i):
        return lct_packet(tsi, args.toi, (i // BLOCK_LEN) << 16 | (i % BLOCK_LEN),
                          content[i * SYMBOL_LEN:(i + 1) * SYMBOL_LEN], len(content))

    # two copies: the first with gaps, the second whole
    middle = (start + end) / 2
    copies = [(start + 0.5, middle, args.gap), (middle, end - 0.5, 0)]
    streams = []
    for first, last, gap in copies:
        sent = [i for i in range(symbols) if not (gap and i % gap == gap - 1)]
        step = (last - first) / max(len(sent), 1)
        streams.append(((first + n * step, data, i) for n, i in enumerate(sent)))
    t = start
    fdts = []
    while t < end:
        fdts.append(t)
        t += FDT_INTERVAL
    streams.append(((ft, None, k) for ft in fdts for k in range(len(fdt_packets))))
    for t, make, i in heapq.merge(*streams, key=lambda x: x[0]):
        yield t, (make(i) if make else fdt_packets[i])


def main():
    parser = argparse.ArgumentParser(description='Put a large file, complete, in a capture\'s object carousel')
    parser.add_argument('input')
    parser.add_argument('output')
    parser.add_argument('--session', default='224.0.49.5:49005', help='carousel address:port (SES videos)')
    parser.add_argument('--tsi', type=int, default=49005)
    parser.add_argument('--location', default='http://dvb.gw/ses.com/private/vod_small/HDplus_ExtraScreen.mp4')
    parser.add_argument('--toi', type=int, default=9)
    parser.add_argument('--type', default='video/mp4')
    parser.add_argument('--size', default='30M', help='size of the generated file (K, M suffixes)')
    parser.add_argument('--file', help='send this file rather than generated content')
    parser.add_argument('--gap', type=int, default=37, help='the first copy misses every Nth packet (0: none)')
    parser.add_argument('--content-out', help='write the file sent here')
    args = parser.parse_args()

    if args.file:
        content = open(args.file, 'rb').read()
    else:
        size = args.size.upper()
        mult = {'K': 1 << 10, 'M': 1 << 20}.get(size[-1:], 1)
        size = int(size[:-1] if size[-1:] in 'KM' else size) * mult
        content = random.Random(20261020).randbytes(size)
    if args.content_out:
        open(args.content_out, 'wb').write(content)

    address, port = args.session.split(':')
    port = int(port)

    # first pass: the capture's span, and a packet of the session to copy the framing from
    header, endian, nano, link, records = read_pcap(args.input)
    start = end = None
    template = None
    for t, frame in records:
        start = t if start is None else start
        end = t
        if template is None and session_of(link, frame) == (address, port, args.tsi):
            template = frame
    if template is None:
        sys.exit('no packets of %s TSI %d in %s' % (args.session, args.tsi, args.input))

    # second pass: the capture without the session, merged with the new session's packets
    header, endian, nano, link, records = read_pcap(args.input)
    kept = ((t, frame) for t, frame in records if session_of(link, frame) != (address, port, args.tsi))
    new = ((t, frame_for(template, link, payload)) for t, payload in generated(args, content, start, end, args.tsi))
    out = open(args.output, 'wb')
    out.write(header)
    count = 0
    for t, frame in heapq.merge(kept, new, key=lambda x: x[0]):
        sec = int(t)
        frac = int(round((t - sec) * (1e9 if nano else 1e6)))
        out.write(struct.pack(endian + 'IIII', sec, frac, len(frame), len(frame)) + frame)
        count += 1
    out.close()
    print('%s: %d packets; %s (%d bytes, MD5 %s) in %s TSI %d, %.0f s' %
          (args.output, count, args.location, len(content), hashlib.md5(content).hexdigest(), args.session,
           args.tsi, end - start))


if __name__ == '__main__':
    main()
