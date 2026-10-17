# vk_host: a headless libretro Vulkan frontend that hashes every frame the
# core presents, for comparing the Vulkan renderer with the GL ones.  See
# vk_host.c.
#
# Linux: the Vulkan loader (libvulkan-dev); runs on lavapipe without a GPU
# (mesa-vulkan-drivers, VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json).
#
#   make
#   ./vk_host ../../neo/boom3_libretro.so <content> <frames> [key=value ...]

CC      ?= cc
CFLAGS  ?= -O1 -g
override CFLAGS += -std=c89 -pedantic -Wall
LIBS     = -ldl -lvulkan -lm
override CFLAGS += -I../../neo/sys/libretro-common/include

TARGET = vk_host

all: $(TARGET)

$(TARGET): vk_host.c
	$(CC) $(CFLAGS) -o $@ vk_host.c $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
