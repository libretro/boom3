# gles_shader_check: every GLES2/GLES3 program the renderers build,
# compiled and linked on a headless GLES 3 context.
#
# Linux: Mesa's EGL and GLES (libegl-dev libgles-dev); runs on llvmpipe
# without a GPU.  Windows (MSYS2 MINGW64): pacman -S
# mingw-w64-x86_64-angleproject, which provides the same libraries.
#
#   make
#   ./gles_shader_check ../../neo/renderer

CC      ?= cc
CFLAGS  ?= -O1 -g
CFLAGS  += -std=c89 -pedantic -Wall -Wno-long-long -Wno-overlength-strings
LIBS     = -lEGL -lGLESv2

TARGET = gles_shader_check

ifneq ($(findstring MINGW,$(shell uname -s 2>/dev/null)),)
TARGET = gles_shader_check.exe
endif

all: $(TARGET)

$(TARGET): check.c
	$(CC) $(CFLAGS) -o $@ check.c $(LIBS)

check: $(TARGET)
	./$(TARGET) ../../neo/renderer

clean:
	rm -f gles_shader_check gles_shader_check.exe

.PHONY: all check clean
