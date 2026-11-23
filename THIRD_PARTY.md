# Third-party code

NippApp is DTVKit Software under the licence in `licence.txt`. This lists the code from
elsewhere that it includes or uses.

## Included in this repository

| Code | Version | Licence | Where |
|---|---|---|---|
| civetweb | 1.16, unchanged from the release (checked against its archive) | MIT (`third_party/civetweb/LICENSE.md`) | `third_party/civetweb/` |
| MD5 by L. Peter Deutsch, in civetweb | as in civetweb 1.16 | as distributed by civetweb; the file has no licence text of its own | `md5.inl` |
| SHA-1 by Steve Reid, in civetweb | as in civetweb 1.16 | public domain (stated in the file) | `sha1.inl` |
| sort_r, in civetweb | as in civetweb 1.16 | as distributed by civetweb; the file has no licence text of its own | `sort.inl` |

civetweb is built without its TLS, CGI, Lua, Duktape and zlib options. Earlier commits also
contain mongoose 3.8 (MIT), which civetweb replaced.

## Linked, not included

| Library | Licence |
|---|---|
| libxml2 | MIT |
| SQLite | public domain |
| libjpeg-turbo | IJG, BSD and zlib |
| libpng | libpng |
| zlib | zlib |
| ICU (through libxml2 on Ubuntu) | Unicode/ICU |
| libavahi-client, libavahi-common | LGPL 2.1 or later; linked dynamically, and optional (`make MDNS=0`) |
| glibc | LGPL 2.1 or later; linked dynamically |

`tools/package.sh` copies libxml2, ICU, SQLite, libjpeg and libpng into its tarball, with their
licences. Avahi and glibc are always the target system's own.

## Player libraries, made by `tools/build-player.sh`

In `player/vendor/`, which isn't in git. Each has its licence beside it.

| Library | Version | Licence |
|---|---|---|
| dvbi-client-js | from its checkout | DTVKit |
| dash.js | 4.7.4 | BSD 3-Clause |
| hls.js | 1.5.20 | Apache 2.0 |

dash.js is distributed with other libraries bundled in it, listed in
`dash.all.min.js.LICENSE.txt` and its package: bcp-47-match, bcp-47-normalize, codem-isoboxer,
es6-promise, fast-deep-equal, html-entities, path-browserify and ua-parser-js 1.0.37 (MIT), imsc
(BSD 2-Clause) and localforage (Apache 2.0). **Check before updating dash.js:** later major
versions of ua-parser-js are AGPL.
