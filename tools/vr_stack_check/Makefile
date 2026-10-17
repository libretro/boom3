# vr_stack_check: the GLES3 post-process programs drawn for each eye,
# each reading only its own half of the stacked _currentRender.
#
# Linux: Mesa's EGL and GLES (libegl-dev libgles-dev); runs on llvmpipe
# without a GPU.  Windows (MSYS2 MINGW64): pacman -S
# mingw-w64-x86_64-angleproject, which provides the same libraries.
#
#   make
#   ./vr_stack_check ../../neo/renderer

CC      ?= cc
CFLAGS  ?= -O1 -g
CFLAGS  += -std=c89 -pedantic -Wall -Wno-long-long -Wno-overlength-strings
LIBS     = -lEGL -lGLESv2

TARGET = vr_stack_check

ifneq ($(findstring MINGW,$(shell uname -s 2>/dev/null)),)
TARGET = vr_stack_check.exe
endif

all: $(TARGET)

$(TARGET): check.c
	$(CC) $(CFLAGS) -o $@ check.c $(LIBS)

check: $(TARGET)
	./$(TARGET) ../../neo/renderer

clean:
	rm -f vr_stack_check vr_stack_check.exe

.PHONY: all check clean
