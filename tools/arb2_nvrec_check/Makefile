# arb2_nvrec_check: the ARB2 NV tiers' interaction parameter record
# (neo/renderer/arb2_nvrec.h) - dirty spans, bitwise comparison, and a
# random walk against a reference buffer.  No GL; any C89 compiler.
#
# Linux, or Windows under MSYS2 MINGW64:
#   make check

CC      ?= cc
CFLAGS  ?= -O1 -g
override CFLAGS += -std=c89 -pedantic -Wall -Wextra -I../../neo/renderer

TARGET = arb2_nvrec_check

all: $(TARGET)

$(TARGET): nvrec_check.c ../../neo/renderer/arb2_nvrec.h
	$(CC) $(CFLAGS) -o $@ nvrec_check.c

check: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe

.PHONY: all check clean
