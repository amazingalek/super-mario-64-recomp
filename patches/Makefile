# The patches (*.c here), compiled for MIPS against the decomp's headers (lib/sm64) and linked into patches.elf, which
# N64Recomp recompiles with patches.toml (CMakeLists.txt's PatchesLib). CC is a clang with a MIPS backend and LD
# ld.lld (on Windows tools/env.sh's MIPS_CC and MIPS_LD).
TARGET = patches.elf

CC      ?= clang
LD      ?= ld.lld

CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
			-fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset \
			-Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas \
			-Wno-unused-variable -Wno-unused-but-set-variable -Wno-missing-braces -Wno-unsupported-floating-point-opt
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS -DTARGET_N64 -DVERSION_US -DF3D_OLD -DNON_MATCHING -DAVOID_UB \
			-I ../lib/sm64/include -I ../lib/sm64/include/libc -I ../lib/sm64/src -I ../lib/sm64 -I ../lib/rt64/include
LDFLAGS  := -nostdlib -T patches.ld -T syms.ld -Map patches.map --unresolved-symbols=ignore-all --emit-relocs

C_SRCS := $(wildcard *.c)
C_OBJS := $(C_SRCS:.c=.o)
C_DEPS := $(C_SRCS:.c=.d)

$(TARGET): $(C_OBJS) patches.ld syms.ld
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

$(C_OBJS): %.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

clean:
	rm -rf $(C_OBJS) $(TARGET) $(C_DEPS)

-include $(C_DEPS)

.PHONY: clean
