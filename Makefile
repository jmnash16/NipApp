# NippApp: headless DVB-NIP gateway built on DVBCore
#
# DVBCore must be built first with DTVKIT_INTEGRATE_DVBNIP=1, DTVKIT_INTEGRATE_SQLITE=1 and
# DTVKIT_USE_STDINT=1 (see DESIGN.md).
#
#   make                 build build/nippapp
#   make DEBUG=0         optimised build
#   make ASAN=1          build with AddressSanitizer
#   make clean

DVBCORE_ROOT ?= $(abspath ../DVBCore)
DVBCORE_LIB  ?= $(DVBCORE_ROOT)/build/bin

BUILD_DIR = build
TARGET    = $(BUILD_DIR)/nippapp

CC ?= cc

SRCS = src/main.c \
       src/search.c \
       src/events.c \
       src/httpd.c \
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
       third_party/mongoose/mongoose.c

INCLUDES = inc \
           platform/hw/inc \
           third_party/mongoose \
           $(DVBCORE_ROOT)/inc \
           $(DVBCORE_ROOT)/platform/inc \
           $(DVBCORE_ROOT)/midware/stb/inc \
           $(DVBCORE_ROOT)/dvb/inc \
           $(DVBCORE_ROOT)/dvbnip/inc

# Must match the DVBCore build so that shared structures have the same layout
DEFINES = OSD_32_BIT COLOUR_DEPTH=32 DVBNIP_BUILD _FILE_OFFSET_BITS=64

# mongoose 3.8 (MIT): no TLS or CGI, and its warnings are not NippApp's
$(BUILD_DIR)/obj/third_party/mongoose/mongoose.o: CFLAGS += -DNO_SSL -DNO_CGI -w

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

LIBS = -L$(DVBCORE_LIB) -ldvbcore -lsqlite3 -lxml2 -ljpeg -lpng -lz -lpthread -lm -ldl

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
