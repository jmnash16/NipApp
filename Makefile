# NippApp: headless DVB-NIP gateway built on DVBCore
#
# DVBCore must be built first with DTVKIT_INTEGRATE_DVBNIP=1, DTVKIT_INTEGRATE_SQLITE=1 and
# DTVKIT_USE_STDINT=1 (see DESIGN.md).
#
#   make                 build build/nippapp
#   make DEBUG=0         optimised build
#   make ASAN=1          build with AddressSanitizer
#   make MDNS=0          build without the mDNS announcement (Avahi)
#   tools/package.sh     build a tarball that runs on other Linux PCs (see DESIGN.md)
#   make clean

DVBCORE_ROOT ?= $(abspath ../DVBCore)
DVBCORE_LIB  ?= $(DVBCORE_ROOT)/build/bin

BUILD_DIR ?= build
TARGET    = $(BUILD_DIR)/nippapp

CC ?= cc

SRCS = src/main.c \
       src/search.c \
       src/events.c \
       src/httpd.c \
       src/registry.c \
       src/encoding.c \
       src/admin.c \
       src/guide.c \
       platform/os/stbos_event.c \
       platform/os/stbos_mutex.c \
       platform/os/stbos_queue.c \
       platform/os/stbos_rtc.c \
       platform/os/stbos_semaphore.c \
       platform/os/stbos_task.c \
       platform/hw/src/celo.c \
       platform/hw/src/stbhwcrypt.c \
       platform/hw/src/stbhwdmx.c \
       platform/hw/src/stbhwdsk.c \
       platform/hw/src/stbhwini.c \
       platform/hw/src/stbhwmem.c \
       platform/hw/src/stbhwnet.c \
       platform/hw/src/stbhwplatform.c \
       platform/hw/src/stbhwsp.c \
       platform/hw/src/stbhwtun.c \
       platform/hw/src/stubs.c \
       third_party/civetweb/civetweb.c

INCLUDES = inc \
           platform/hw/inc \
           third_party/civetweb \
           $(DVBCORE_ROOT)/inc \
           $(DVBCORE_ROOT)/platform/inc \
           $(DVBCORE_ROOT)/midware/stb/inc \
           $(DVBCORE_ROOT)/dvb/inc \
           $(DVBCORE_ROOT)/dvbnip/inc \
           /usr/include/libxml2

# Must match the DVBCore build so that shared structures have the same layout
DEFINES = OSD_32_BIT COLOUR_DEPTH=32 DVBNIP_BUILD _FILE_OFFSET_BITS=64

# civetweb 1.16 (MIT): no TLS, CGI or file serving (NippApp answers every request itself), and its
# warnings are not NippApp's
$(BUILD_DIR)/obj/third_party/civetweb/civetweb.o: CFLAGS += -DNO_SSL -DNO_CGI -DNO_FILES -DNO_CACHING -w

CFLAGS += -Wall $(addprefix -D,$(DEFINES)) $(addprefix -I,$(INCLUDES)) -MMD -MP
ifeq ($(DEBUG),0)
CFLAGS += -O2
else
CFLAGS += -g
endif
ifeq ($(ASAN),1)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif

# Libraries are looked for in ../lib next to the executable first, as in the tarball from
# tools/package.sh (bin/nippapp, lib/). An RPATH rather than a RUNPATH, so that the libraries'
# own dependencies (libxml2's ICU) are found there too. A build has no ../lib, so it uses the system's.
LDFLAGS += -Wl,-rpath,'$$ORIGIN/../lib' -Wl,--disable-new-dtags

LIBS = -L$(DVBCORE_LIB) -ldvbcore -lsqlite3 -lxml2 -ljpeg -lpng -lz -lpthread -lm -ldl

# The mDNS announcement uses avahi-daemon through libavahi-client (libavahi-client-dev); without
# it NippApp is built without the announcement. MDNS=0 leaves it out anyway.
ifneq ($(MDNS),0)
ifeq ($(shell pkg-config --exists avahi-client && echo yes),yes)
SRCS += src/mdns.c
DEFINES += NIPPAPP_MDNS
LIBS += $(shell pkg-config --libs avahi-client)
endif
endif

OBJS = $(SRCS:%.c=$(BUILD_DIR)/obj/%.o)

all: $(TARGET)

$(TARGET): $(OBJS) $(DVBCORE_LIB)/libdvbcore.a
	@echo Linking $@
	@$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	@echo Compiling $<
	@$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean

-include $(OBJS:.o=.d)
