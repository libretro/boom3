# Background reader read-step regression test: a cancel or the reader's
# exit must take effect within one chunk, and a stopped read must resume
# exactly. Builds against the core's own bgread.c and rzip.cpp and the
# vendored libretro-common. Needs zip(1) to make its pk4.
NEO     = ../../neo
LRC     = $(NEO)/sys/libretro-common
CC     ?= cc
CXX    ?= c++
CFLAGS ?= -O2
CPPFLAGS += -I$(NEO) -I$(LRC)/include -Wall
LRC_SRCS = \
	$(LRC)/streams/file_stream.c \
	$(LRC)/vfs/vfs_implementation.c \
	$(LRC)/vfs/vfs_hybrid.c \
	$(LRC)/encodings/encoding_deflate.c \
	$(LRC)/encodings/encoding_crc32.c \
	$(LRC)/encodings/encoding_utf.c \
	$(LRC)/file/file_path.c \
	$(LRC)/file/file_path_io.c \
	$(LRC)/compat/compat_strl.c \
	$(LRC)/compat/compat_posix_string.c \
	$(LRC)/compat/compat_strcasestr.c \
	$(LRC)/compat/fopen_utf8.c \
	$(LRC)/string/stdstring.c \
	$(LRC)/time/rtime.c \
	$(LRC)/memmap/memalign.c \
	$(LRC)/memmap/memmap.c \
	$(LRC)/string/rstrtod.c \
	$(LRC)/file/retro_dirent.c \
	$(LRC)/features/features_cpu.c
OBJS = bgread_test.o bgread.o rzip.o $(notdir $(LRC_SRCS:.c=.o))

vpath %.c $(NEO)/framework $(sort $(dir $(LRC_SRCS)))
vpath %.cpp $(NEO)/framework

bgread_test: $(OBJS)
	$(CXX) $(CFLAGS) -o $@ $(OBJS) -lm

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

check: bgread_test
	mkdir -p work && ./bgread_test work

clean:
	rm -rf bgread_test *.o work

.PHONY: check clean
