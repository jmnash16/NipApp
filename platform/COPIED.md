# Files copied from App

NippApp keeps its own copy of these platform files so that it depends only on
DVBCore. Fixes made to them in App have to be copied across by hand. All were
copied on 8 Oct 2026 from App at `82bebdb`. The paths below are relative to
`App/platform`.

| NippApp file | Copied from | Changes |
|---|---|---|
| `os/stbos_event.c` | `os/linux/src/stbos_event.c` | none |
| `os/stbos_mutex.c` | `os/linux/src/stbos_mutex.c` | none |
| `os/stbos_queue.c` | `os/linux/src/stbos_queue.c` | none |
| `os/stbos_rtc.c` | `os/linux/src/stbos_rtc.c` | none |
| `os/stbos_semaphore.c` | `os/linux/src/stbos_semaphore.c` | none |
| `os/stbos_task.c` | `os/linux/src/stbos_task.c` | none |
| `hw/inc/stbhwdef.h` | `hw/linux/inc/stbhwdef.h` | `NUM_TUNER_PATHS` is 2 |
| `hw/inc/types_int.h` | `hw/linux/inc/types_int.h` | none |
| `hw/src/celo.c`, `hw/src/celo.h` | `hw/linux/src/` | none |
| `hw/src/stbhwcrypt.c` | `hw/linux/src/stbhwcrypt.c` | none |
| `hw/src/stbhwdsk.c` | `hw/linux/src/stbhwdsk.c` | none |
| `hw/src/stbhwini.c` | `hw/linux/src/stbhwini.c` | no signal trapping: `main()` handles signals and saves the database |
| `hw/src/stbhwmem.c` | `hw/linux/src/stbhwmem.c` | none |
| `hw/src/stbhwnet.c` | `hw/linux/src/stbhwnet.c` | none |
| `hw/src/stbhwplatform.c` | `hw/linux/src/stbhwplatform.c` | none |
| `hw/src/stbhwsp.c` | `hw/linux/src/stbhwsp.c` | none |

These files are NippApp's own and are not copies:

- `hw/src/stbhwtun.c`: the file tuner. It reads the sources file and feeds
  captures in real time: TS to the demux, paced by PCR or by TDT when there is
  no PCR; PCAP as GSE to DVBCore's GSE callback, paced by capture timestamps.
- `hw/src/stbhwdmx.c`: the demux. Section filtering only, using App's filter
  ID scheme, with no audio, video or teletext PES handling.
- `hw/inc/nipphw.h`: the interface between the tuner and the demux.
- `hw/src/stubs.c`: AV, OSD, PVR, front panel, VBI and EBU functions, which a
  headless gateway does not need.

Many of the copied files are ISO-8859-1 encoded. Use `grep -a` on them, or
matches are silently skipped.
