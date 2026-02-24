STATIC_LINKING := 0
AR             := ar
CORE           := -DCORE=ON
SDL_VER        := 2
IMGGUI         := 1
OPENAL         := 1
DEBUG          := 0
RELWITHDEBINFO := 0

ifneq ($(V),1)
   Q := @
endif

ifneq ($(SANITIZER),)
   CFLAGS   := -fsanitize=$(SANITIZER) $(CFLAGS)
   CXXFLAGS := -fsanitize=$(SANITIZER) $(CXXFLAGS)
   LDFLAGS  := -fsanitize=$(SANITIZER) $(LDFLAGS)
endif

ifeq ($(platform),)
platform = unix
ifeq ($(shell uname -a),)
   platform = win
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
   platform = osx
else ifneq ($(findstring win,$(shell uname -a)),)
   platform = win
endif
endif

# system platform
system_platform = unix
ifeq ($(shell uname -a),)
	EXE_EXT = .exe
	system_platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
	system_platform = osx
	arch = intel
ifeq ($(shell uname -p),powerpc)
	arch = ppc
endif
else ifneq ($(findstring MINGW,$(shell uname -a)),)
	system_platform = win
endif

CORE_DIR    += .
ifeq ($(D3XP), 1)
TARGET_NAME := boom3_xp
else
TARGET_NAME := boom3
endif
LIBM		    = -lm

ifeq ($(ARCHFLAGS),)
ifeq ($(archs),ppc)
   ARCHFLAGS = -arch ppc -arch ppc64
else
   ARCHFLAGS = -arch i386 -arch x86_64
endif
endif

ifeq ($(platform), osx)
ifndef ($(NOUNIVERSAL))
   CXXFLAGS += $(ARCHFLAGS)
   LFLAGS += $(ARCHFLAGS)
endif
endif

ifeq ($(STATIC_LINKING), 1)
EXT := a
endif

ifneq (,$(findstring unix,$(platform)))
	EXT ?= so
   TARGET := $(TARGET_NAME)_libretro.$(EXT)
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
   LIBS := -ldl -lpthread

	GL_LIB := -lGL
else ifeq ($(platform), linux-portable)
   TARGET := $(TARGET_NAME)_libretro.$(EXT)
   fpic := -fPIC -nostdlib
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T
	LIBM :=
	GL_LIB := -lGL
else ifneq (,$(findstring osx,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   GL_LIB := -framework OpenGL
else ifneq (,$(findstring ios,$(platform)))
   TARGET := $(TARGET_NAME)_libretro_ios.dylib
	fpic := -fPIC
	SHARED := -dynamiclib

ifeq ($(IOSSDK),)
   IOSSDK := $(shell xcodebuild -version -sdk iphoneos Path)
endif

	DEFINES := -DIOS
	CC = cc -arch armv7 -isysroot $(IOSSDK)
ifeq ($(platform),ios9)
CC     += -miphoneos-version-min=8.0
CXXFLAGS += -miphoneos-version-min=8.0
else
CC     += -miphoneos-version-min=5.0
CXXFLAGS += -miphoneos-version-min=5.0
endif
else ifneq (,$(findstring qnx,$(platform)))
	TARGET := $(TARGET_NAME)_libretro_qnx.so
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
else ifeq ($(platform), emscripten)
   TARGET := $(TARGET_NAME)_libretro_emscripten.bc
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
else ifeq ($(platform), vita)
   TARGET := $(TARGET_NAME)_vita.a
   CC = arm-vita-eabi-gcc
   AR = arm-vita-eabi-ar
   CXXFLAGS += -Wl,-q -Wall -O3
   STATIC_LINKING = 1
# Nintendo Switch (libnx)
else ifeq ($(platform), libnx)
    include $(DEVKITPRO)/libnx/switch_rules
    EXT=a
    TARGET := $(TARGET_NAME)_libretro_$(platform).$(EXT)
    DEFINES := -DSWITCH=1 -D__SWITCH__=1 -DHAVE_LIBNX=1
    CFLAGS	:=	 $(DEFINES) -g -fPIE -I$(LIBNX)/include/ -I$(PORTLIBS)/include/ -ffunction-sections -fdata-sections -ftls-model=local-exec
    CFLAGS += $(INCDIRS) -I$(CORE_DIR)/zlib -I$(CORE_DIR)/libogg/include -I$(CORE_DIR)/libvorbis/include \
		-I$(CORE_DIR)/jpeg-8c -fvisibility=hidden
    CFLAGS	+= -march=armv8-a -mtune=cortex-a57 -mtp=soft -mcpu=cortex-a57+crc+fp+simd
    CXXFLAGS := $(ASFLAGS) $(CFLAGS) -std=gnu++11
    STATIC_LINKING = 1
else
   CC ?= gcc
   TARGET := $(TARGET_NAME)_libretro.dll
   SHARED := -shared -static-libgcc -static-libstdc++ -s -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
	GL_LIB := -lopengl32
   LDFLAGS += -lwsock32 -lws2_32
endif

# Normalize COMPILE_ARCH to match CMake's cpu normalization
ifeq ($(COMPILE_ARCH),powerpc)
    D3_ARCH := ppc
else ifeq ($(COMPILE_ARCH),powerpc64)
    D3_ARCH := ppc64
else ifeq ($(COMPILE_ARCH),aarch64)
    D3_ARCH := arm64
else ifeq ($(COMPILE_ARCH),amd64)
    D3_ARCH := x86_64
else ifeq ($(COMPILE_ARCH),x64)
    D3_ARCH := x86_64
else ifneq (,$(shell echo $(COMPILE_ARCH) | grep -E '^i.86$$'))
    D3_ARCH := x86
else ifneq (,$(shell echo $(COMPILE_ARCH) | grep -iE '^arm'))
    ifeq ($(D3_SIZEOFPTR),8)
        D3_ARCH := arm64
    else
        D3_ARCH := arm
    endif
else
    D3_ARCH := $(COMPILE_ARCH)
endif

# Equivalent of CMake's D3_OSTYPE and D3_SIZEOFPTR
ifeq ($(platform), osx)
    D3_OSTYPE := macosx
else ifeq ($(platform), win)
    D3_OSTYPE := windows
else ifneq (,$(findstring ios,$(platform)))
    D3_OSTYPE := ios
else
    D3_OSTYPE := linux
endif

# Pointer size: check for known 32-bit architectures, otherwise assume 64-bit
ifneq (,$(filter $(COMPILE_ARCH), x86 arm ppc))
    D3_SIZEOFPTR := 4
else
    D3_SIZEOFPTR := 8
endif

CFLAGS += -DD3_OSTYPE=\"$(D3_OSTYPE)\" -DD3_SIZEOFPTR=$(D3_SIZEOFPTR) -DD3_ARCH=\"$(D3_ARCH)\"

# Detect endianness by compiling and running a small probe
ENDIAN_PROBE := $(shell printf 'int main(){int x=1;return *(char*)&x;}' | $(CC) -x c - -o /tmp/endian_probe 2>/dev/null && /tmp/endian_probe; echo $$?)

ifeq ($(ENDIAN_PROBE),0)
    D3_IS_BIG_ENDIAN := 1
    $(info Detected Big Endian architecture, setting -DD3_IS_BIG_ENDIAN=1)
else
    D3_IS_BIG_ENDIAN := 0
    $(info Detected Little Endian architecture, setting -DD3_IS_BIG_ENDIAN=0)
endif

CFLAGS += -DD3_IS_BIG_ENDIAN=$(D3_IS_BIG_ENDIAN)

HAVE_OPENGL = 1

LDFLAGS += $(LIBM)

ifeq ($(DEBUG),1)
  CFLAGS += -O0 -g -DDEBUG
  CXXFLAGS += -O0 -g -DDEBUG
else ifeq ($(RELWITHDEBINFO),1)
  CFLAGS  += -O2 -g -fno-omit-frame-pointer
  CXXFLAGS += -O2 -g -fno-omit-frame-pointer
else
  CFLAGS += -O3
  CXXFLAGS += -O3
endif

include Makefile.common

OBJECTS := $(SOURCES_C:.c=.o) $(SOURCES_CXX:.cpp=.o)
OBJECTS := $(filter-out %.c %.cpp,$(OBJECTS))

COMPILE_PLATFORM=$(shell uname | sed -e 's/_.*//' | tr '[:upper:]' '[:lower:]' | sed -e 's/\//_/g')
COMPILE_ARCH := $(shell uname -m | sed -e 's/i.86/x86/' -e 's/amd64/x86_64/' -e 's/x64/x86_64/' -e 's/powerpc$$/ppc/' -e 's/powerpc64/ppc64/' -e 's/aarch64/arm64/')

ifeq ($(COMPILE_PLATFORM),cygwin)
  PLATFORM=mingw32
endif

ifndef PLATFORM
PLATFORM=$(COMPILE_PLATFORM)
endif
export PLATFORM

ifeq ($(PLATFORM),mingw32)
  MINGW=1
endif
ifeq ($(PLATFORM),mingw64)
  MINGW=1
endif

ifeq ($(COMPILE_ARCH),i86pc)
  COMPILE_ARCH=x86
endif

ifeq ($(COMPILE_ARCH),amd64)
  COMPILE_ARCH=x86_64
endif
ifeq ($(COMPILE_ARCH),x64)
  COMPILE_ARCH=x86_64
endif

ifeq ($(COMPILE_ARCH),powerpc)
  COMPILE_ARCH=ppc
endif
ifeq ($(COMPILE_ARCH),powerpc64)
  COMPILE_ARCH=ppc64
endif

ifeq ($(COMPILE_ARCH),axp)
  COMPILE_ARCH=alpha
endif

TOPDIR ?= $(CURDIR)

ifeq ($(SDL_VER),2)
    SDL2 := -DSDL2=ON -DHAVE_SDL
else ifeq ($(SDL_VER),3)
    SDL2 := -DD3_SDL3 -DHAVE_SDL
endif

ifeq ($(IMGGUI),0)
    CFLAGS   += -DIMGUI_DISABLE
    CXXFLAGS += -DIMGUI_DISABLE
endif

CFLAGS   += -Wall -D__LIBRETRO__ $(fpic) -DCORE=ON -DSDL2=ON -DBASE=ON -DUSE_FILE32API -ffast-math \
			-fno-unsafe-math-optimizations -fno-strict-aliasing -fomit-frame-pointer \
			-Wno-sign-compare -Wno-switch -Wno-format-security -I$(TOPDIR) -I$(TOPDIR)/sys/libretro/config
CXXFLAGS += -Wall -D__LIBRETRO__ $(fpic) -fpermissive -std=gnu++11

ifeq ($(D3XP), 1)
	CFLAGS	 +=	-I$(TOPDIR)/d3xp -D_D3XP
	CXXFLAGS +=     -I$(TOPDIR)/d3xp -D_D3XP
else
	CFLAGS	+=	-I$(TOPDIR)/game
endif

CFLAGS     += $(INCFLAGS)
CXXFLAGS   += $(INCFLAGS)

all: $(TARGET)

$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CXX) $(fpic) $(SHARED) -o $@ $(OBJECTS) $(LDFLAGS) $(GL_LIB) $(LIBS) $(SDL_LIBS) $(OPENAL_LIBS)
endif

%.o: $(CORE_DIR)/%.cpp
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(fpic) -c -o $@ $<

%.o: $(CORE_DIR)/%.c
	$(CC) $(CFLAGS) -std=gnu11 $(fpic) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: clean

print-%:
	@echo '$*=$($*)'
