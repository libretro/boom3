STATIC_LINKING := 0
AR             := ar
CORE           := -DCORE=ON
SDL_VER        := 0
IMGGUI         := 0
OPENAL         := 0
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
else ifneq (,$(findstring mingw,$(CXX)))
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
else ifeq ($(shell uname -m),arm64)
   ARCHFLAGS = -arch arm64
else
   ARCHFLAGS = -arch x86_64
endif
endif

ifeq ($(platform), osx)
ifndef ($(NOUNIVERSAL))
   CXXFLAGS += $(ARCHFLAGS)
   LDFLAGS += $(ARCHFLAGS)
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
else ifeq ($(platform), osx-arm64)
   TARGET := $(TARGET_NAME)_libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   GL_LIB := -framework OpenGL
   CC = cc -arch arm64
   CXX = c++ -arch arm64
   CFLAGS += -Wno-deprecated-declarations -Wno-macro-redefined
   CXXFLAGS += -Wno-deprecated-declarations -Wno-macro-redefined
else ifeq ($(platform), osx-x64)
   TARGET := $(TARGET_NAME)_libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   GL_LIB := -framework OpenGL
   CC = cc -arch x86_64
   CXX = c++ -arch x86_64
   CFLAGS += -Wno-deprecated-declarations -Wno-macro-redefined
   CXXFLAGS += -Wno-deprecated-declarations -Wno-macro-redefined
else ifneq (,$(findstring osx,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   GL_LIB := -framework OpenGL
   CFLAGS += -Wno-deprecated-declarations -Wno-macro-redefined
   CXXFLAGS += -Wno-deprecated-declarations -Wno-macro-redefined
else ifeq ($(platform), ios-arm64)
   TARGET := $(TARGET_NAME)_libretro_ios.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   GL_LIB := -framework OpenGLES
   GLES = 1
   CORE =
   ifeq ($(IOSSDK),)
      IOSSDK := $(shell xcodebuild -version -sdk iphoneos Path)
   endif
   CC = cc -arch arm64 -isysroot $(IOSSDK) -miphoneos-version-min=11.0
   CXX = c++ -arch arm64 -isysroot $(IOSSDK) -miphoneos-version-min=11.0
   CFLAGS += -DHAVE_OPENGLES -DHAVE_OPENGLES2 -DIOS -DGLES_SILENCE_DEPRECATION
   CXXFLAGS += -DHAVE_OPENGLES -DHAVE_OPENGLES2 -DIOS -DGLES_SILENCE_DEPRECATION
else ifeq ($(platform), tvos-arm64)
   TARGET := $(TARGET_NAME)_libretro_tvos.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   GL_LIB := -framework OpenGLES
   GLES = 1
   CORE =
   ifeq ($(TVOSSDK),)
      TVOSSDK := $(shell xcodebuild -version -sdk appletvos Path)
   endif
   CC = cc -arch arm64 -isysroot $(TVOSSDK) -mtvos-version-min=11.0
   CXX = c++ -arch arm64 -isysroot $(TVOSSDK) -mtvos-version-min=11.0
   CFLAGS += -DHAVE_OPENGLES -DHAVE_OPENGLES2 -DIOS -DGLES_SILENCE_DEPRECATION
   CXXFLAGS += -DHAVE_OPENGLES -DHAVE_OPENGLES2 -DIOS -DGLES_SILENCE_DEPRECATION
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
else ifneq (,$(findstring android,$(platform)))
   TARGET := $(TARGET_NAME)_libretro_android.so
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
   LIBS := -ldl
   GL_LIB := -lGLESv2
   CFLAGS += -DHAVE_OPENGLES -DHAVE_OPENGLES2
   CXXFLAGS += -DHAVE_OPENGLES -DHAVE_OPENGLES2
   LDFLAGS += -static-libstdc++
   CORE =
   GLES = 1
   ifeq ($(origin CC), default)
      ifneq ($(ANDROID_NDK_LLVM),)
        NDK_BIN := $(ANDROID_NDK_LLVM)/bin
        API := 21
        TARGET_TRIPLE := aarch64-linux-android
        CC  := $(NDK_BIN)/$(TARGET_TRIPLE)$(API)-clang
        CXX := $(NDK_BIN)/$(TARGET_TRIPLE)$(API)-clang++
      endif
   endif
else
# Windows
   CC ?= gcc
   TARGET := $(TARGET_NAME)_libretro.dll
   SHARED := -shared -static-libgcc -static-libstdc++ -s -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
   GL_LIB := -lopengl32
   LDFLAGS += -lwsock32 -lws2_32
   LIBS :=
   CFLAGS += -DAL_LIBTYPE_STATIC -DALC_LIBTYPE_STATIC
   CXXFLAGS += -DAL_LIBTYPE_STATIC -DALC_LIBTYPE_STATIC
endif

# 1. If CROSS_COMPILE is a real prefix (contains a dash), derive COMPILE_ARCH from it
ifneq ($(findstring -,$(CROSS_COMPILE)),)
    COMPILE_ARCH := $(firstword $(subst -, ,$(CROSS_COMPILE)))
endif

# 2. If platform is android, derive from CC instead
ifeq ($(strip $(COMPILE_ARCH)),)
ifneq (,$(findstring android,$(platform)))
    COMPILE_ARCH := $(firstword $(subst -, ,$(notdir $(CC))))
endif
endif

# 3. If still empty, try CC -dumpmachine (catches i386 docker on x86_64 host)
ifeq ($(strip $(COMPILE_ARCH)),)
    COMPILE_ARCH := $(shell $(CC) -dumpmachine 2>/dev/null | cut -d- -f1)
endif

# 4. If still empty, fall back to uname -m
ifeq ($(strip $(COMPILE_ARCH)),)
    COMPILE_ARCH := $(shell uname -m | sed -e 's/amd64/x86_64/' -e 's/x64/x86_64/' -e 's/powerpc$$/ppc/' -e 's/powerpc64/ppc64/' -e 's/aarch64/arm64/')
endif

ifeq ($(platform),libnx)
    COMPILE_ARCH := aarch64
endif

# Normalize i?86 to x86
COMPILE_ARCH := $(shell echo $(COMPILE_ARCH) | sed 's/i[3456]86/x86/')

# Default to 64-bit pointer size
D3_SIZEOFPTR := 8

ifeq ($(COMPILE_ARCH),powerpc)
    D3_ARCH := ppc
    D3_IS_BIG_ENDIAN := 1
    D3_SIZEOFPTR := 4
else ifeq ($(COMPILE_ARCH),powerpc64)
    D3_ARCH := ppc64
    D3_IS_BIG_ENDIAN := 1
else ifeq ($(COMPILE_ARCH),aarch64)
    D3_ARCH := aarch64
    D3_IS_BIG_ENDIAN := 0
else ifeq ($(COMPILE_ARCH),arm64)
    D3_ARCH := aarch64
    D3_IS_BIG_ENDIAN := 0
else ifeq ($(COMPILE_ARCH),x86_64)
    D3_ARCH := x86_64
    D3_IS_BIG_ENDIAN := 0
else ifeq ($(COMPILE_ARCH),x86)
    D3_ARCH := x86
    D3_IS_BIG_ENDIAN := 0
    D3_SIZEOFPTR := 4
else ifneq (,$(findstring arm,$(COMPILE_ARCH)))
    D3_ARCH := arm
    D3_IS_BIG_ENDIAN := 0
    D3_SIZEOFPTR := 4
else ifneq (,$(findstring mips,$(COMPILE_ARCH)))
    D3_ARCH := mips
    D3_IS_BIG_ENDIAN := 0
else ifneq (,$(findstring sparc,$(COMPILE_ARCH)))
    D3_ARCH := sparc
    D3_IS_BIG_ENDIAN := 1
else
    D3_ARCH := $(COMPILE_ARCH)
    D3_IS_BIG_ENDIAN := 0
endif

# Equivalent of CMake's D3_OSTYPE and D3_SIZEOFPTR
ifneq (,$(findstring osx,$(platform)))
    D3_OSTYPE := macosx
else ifeq ($(platform), win)
    D3_OSTYPE := windows
else ifneq (,$(findstring mingw,$(CXX)))
    D3_OSTYPE := windows
else ifneq (,$(findstring tvos,$(platform)))
    D3_OSTYPE := tvos
else ifneq (,$(findstring ios,$(platform)))
    D3_OSTYPE := ios
else
    D3_OSTYPE := linux
endif

CFLAGS += -DD3_OSTYPE=\"$(D3_OSTYPE)\" -DD3_SIZEOFPTR=$(D3_SIZEOFPTR) -DD3_ARCH=\"$(D3_ARCH)\"
CFLAGS += -DD3_IS_BIG_ENDIAN=$(D3_IS_BIG_ENDIAN)

# webOS
ifneq (,$(or $(findstring webos,$(CROSS_COMPILE)),$(findstring starfish,$(CROSS_COMPILE))))
   GLES = 1
   # CORE is for OpenGL or GLES 3.x
   CORE =
   GL_LIB := -lGLESv2
   CFLAGS += -DHAVE_OPENGLES -DHAVE_OPENGLES2
   CXXFLAGS += -DHAVE_OPENGLES -DHAVE_OPENGLES2
endif

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

ifeq ($(OPENAL),1)
    CFLAGS   += -DHAVE_OPENAL
    CXXFLAGS += -DHAVE_OPENAL
endif

CFLAGS   += -Wall -D__LIBRETRO__ $(fpic) $(CORE) $(SDL2) -DBASE=ON -DUSE_FILE32API -ffast-math \
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
