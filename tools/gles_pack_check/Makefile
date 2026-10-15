# gles_pack_check: the GLES texture packings against plain RGBA, sample for
# sample, in GLES 2 and GLES 3 contexts.
#
# Linux: Mesa's EGL and GLES (libegl-dev libgles-dev); runs on llvmpipe
# without a GPU.
#
#   make check

CC      ?= cc
CFLAGS  ?= -O1 -g
override CFLAGS += -std=c89 -pedantic -Wall -I../../neo/renderer
# llvmpipe's JIT resolves the math library from the process
LIBS     = -lEGL -lGLESv2 -Wl,--no-as-needed -lm

TARGET = gles_pack_check

all: $(TARGET)

$(TARGET): pack_check.c ../../neo/renderer/gles_pack.c ../../neo/renderer/gles_pack.h
	$(CC) $(CFLAGS) -o $@ pack_check.c ../../neo/renderer/gles_pack.c $(LIBS)

check: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all check clean
