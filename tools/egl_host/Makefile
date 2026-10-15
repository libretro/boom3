# egl_host: a headless libretro frontend that hashes every frame the core
# presents, for comparing renderers.  See egl_host.c.
#
# Linux: Mesa's EGL and GL (libegl-dev libgl-dev libgles-dev); runs on
# llvmpipe without a GPU.
#
#   make
#   ./egl_host ../../neo/boom3_libretro.so <content> <frames> [key=value ...]

CC      ?= cc
CFLAGS  ?= -O1 -g
override CFLAGS += -std=c89 -pedantic -Wall
# llvmpipe's JIT resolves the math library from the process
LIBS     = -ldl -lEGL -Wl,--no-as-needed -lm

TARGET = egl_host

all: $(TARGET)

$(TARGET): egl_host.c
	$(CC) $(CFLAGS) -o $@ egl_host.c $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
