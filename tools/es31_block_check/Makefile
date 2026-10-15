# es31_block_check: the ES 3.1 renderer's block-fed interaction programs
# against the ES 3 renderer's uniform-fed ones, pixel for pixel.
#
# Linux: Mesa's EGL and GLES (libegl-dev libgles-dev); runs on llvmpipe
# without a GPU.  Windows (MSYS2 MINGW64): pacman -S
# mingw-w64-x86_64-angleproject, which provides the same libraries.
#
#   make
#   ./es31_block_check ../../neo/renderer

CC      ?= cc
CFLAGS  ?= -O1 -g
CFLAGS  += -std=c89 -pedantic -Wall -Wno-long-long -Wno-overlength-strings -I../../neo/renderer
LIBS     = -lEGL -lGLESv2

TARGET = es31_block_check

ifneq ($(findstring MINGW,$(shell uname -s 2>/dev/null)),)
TARGET = es31_block_check.exe
else
# llvmpipe's JIT resolves the math library from the process; without it
# loaded the first shader calling pow() jumps to address 0
LIBS    += -Wl,--no-as-needed -lm
endif

all: $(TARGET)

$(TARGET): block_check.c ../../neo/renderer/glsl_blocks.c ../../neo/renderer/glsl_blocks.h
	$(CC) $(CFLAGS) -o $@ block_check.c ../../neo/renderer/glsl_blocks.c $(LIBS)

check: $(TARGET)
	./$(TARGET) ../../neo/renderer

clean:
	rm -f es31_block_check es31_block_check.exe

.PHONY: all check clean
