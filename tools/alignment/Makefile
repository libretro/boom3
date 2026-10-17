# Alignment regression tests for the engine's bump allocators: the frame
# memory block (base 16 byte aligned on every pointer width, also at the
# 32-bit layout when the toolchain has -m32) and the collision model
# polygon stride (8 byte aligned).
NEO     = ../../neo
CC     ?= cc
CXX    ?= c++
CFLAGS ?= -O2
CPPFLAGS += -I$(NEO) -I$(NEO)/sys/libretro/config -I$(NEO)/sys/libretro-common/include -Wall

all: frame_block_test cm_polygon_test

frame_block_test: frame_block_test.c $(NEO)/renderer/frame_block.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ frame_block_test.c

cm_polygon_test: cm_polygon_test.cpp $(NEO)/cm/CollisionModel_local.h
	$(CXX) $(CFLAGS) $(CPPFLAGS) -std=gnu++11 -fpermissive -w -o $@ cm_polygon_test.cpp

check: all
	./frame_block_test
	./cm_polygon_test
	@if echo 'int main(void){return 0;}' | $(CC) -m32 -x c - -o /dev/null 2>/dev/null; then \
		$(CC) $(CFLAGS) $(CPPFLAGS) -m32 -o frame_block_test32 frame_block_test.c && ./frame_block_test32; \
	else echo "no -m32 toolchain, 32-bit layout not checked"; fi

clean:
	rm -f frame_block_test frame_block_test32 cm_polygon_test *.exe

.PHONY: all check clean
