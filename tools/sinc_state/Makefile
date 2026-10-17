# Sinc resampler stream-state regression test (savestate continuity and
# handle-pool reset). Builds against the core's vendored libretro-common.
LRC     = ../../neo/sys/libretro-common
CC     ?= cc
CFLAGS ?= -O2
CPPFLAGS += -I$(LRC)/include -Wall
SRCS    = sinc_state_test.c \
	$(LRC)/audio/resampler/drivers/sinc_resampler.c \
	$(LRC)/features/features_cpu.c \
	$(LRC)/memmap/memalign.c \
	$(LRC)/compat/compat_strl.c \
	$(LRC)/string/stdstring.c \
	$(LRC)/encodings/encoding_utf.c \
	$(LRC)/time/rtime.c

ifneq (,$(filter aarch64 arm64,$(shell uname -m)))
SRCS   += $(LRC)/audio/resampler/drivers/sinc_resampler_neon.S
CFLAGS += -DHAVE_NEON
endif

sinc_state_test: $(SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRCS) -lm

check: sinc_state_test
	./sinc_state_test

clean:
	rm -f sinc_state_test

.PHONY: check clean
