# -----------------------------------------------------------------------
# Project Information
# -----------------------------------------------------------------------

PROJECT_IDX	= 5

# -----------------------------------------------------------------------
# Include Platform-specific Configuration
# -----------------------------------------------------------------------

-include config.mk

# -----------------------------------------------------------------------
# Host Linux Variables (defaults, can be overridden by config.mk)
# -----------------------------------------------------------------------

SHELL       = /bin/sh
DISK        ?= /dev/sdb
DISK_SECTOR ?= 3
TTYUSB1     ?= /dev/ttyUSB1
DIR_OSLAB   ?= $(HOME)/OSLab-RISC-V
DIR_QEMU    ?= $(DIR_OSLAB)/qemu
DIR_UBOOT   ?= $(DIR_OSLAB)/u-boot

# -----------------------------------------------------------------------
# Build and Debug Tools
# -----------------------------------------------------------------------

HOST_CC         = gcc
HOST_GDB	= gdb
CROSS_PREFIX    ?= riscv64-unknown-linux-gnu-
CC              = $(CROSS_PREFIX)gcc
AR              = $(CROSS_PREFIX)ar
OBJDUMP         = $(CROSS_PREFIX)objdump
GDB             = $(CROSS_PREFIX)gdb
QEMU            ?= $(DIR_QEMU)/riscv64-softmmu/qemu-system-riscv64
UBOOT           ?= $(DIR_UBOOT)/u-boot
MINICOM         ?= minicom

# -----------------------------------------------------------------------
# Build/Debug Flags and Variables
# -----------------------------------------------------------------------

COMMON_FLAGS    = -fno-builtin -nostdlib -nostdinc -Wall -mcmodel=medany -ggdb3
COMMON_FLAGS    += -g
COMMON_FLAGS    += -O2
# COMMON_FLAGS    += -DNOLOG
# COMMON_FLAGS    += -DNOASSERTS

CFLAGS          = -std=gnu11
CFLAGS          += $(COMMON_FLAGS)
CXXFLAGS        = -std=gnu++20 -fno-exceptions -fno-rtti -Wno-register
CXXFLAGS        += $(COMMON_FLAGS)

BOOT_INCLUDE    = -I$(DIR_ARCH)/include
BOOT_CFLAGS     = $(CFLAGS) $(BOOT_INCLUDE) -Wl,--defsym=TEXT_START=$(BOOTLOADER_ENTRYPOINT) -T riscv.lds

KERNEL_INCLUDE  = -I$(DIR_ARCH)/include -Iinclude -Idrivers
KERNEL_LDFLAGS  = -nostdlib -nostdinc -Wl,--defsym=TEXT_START=$(KERNEL_ENTRYPOINT) -T riscv.lds
KERNEL_CFLAGS   = $(CFLAGS) $(KERNEL_INCLUDE)
KERNEL_CXXFLAGS = $(CXXFLAGS) $(KERNEL_INCLUDE)

USER_INCLUDE    = -I$(DIR_TINYLIBC)/include
USER_CFLAGS     = $(CFLAGS) $(USER_INCLUDE)
USER_LDFLAGS    = -L$(DIR_BUILD) -ltinyc

QEMU_LOG_FILE   = $(DIR_OSLAB)/oslab-log.txt
QEMU_OPTS       = -nographic -machine virt -m 256M -kernel $(UBOOT) -bios none \
                     -drive if=none,format=raw,id=image,file=${ELF_IMAGE} \
                     -device virtio-blk-device,drive=image \
                     -monitor telnet::45454,server,nowait -serial mon:stdio \
                     -D $(QEMU_LOG_FILE) -d oslab
QEMU_DEBUG_OPT  = -s -S
QEMU_SMP_OPT	= -smp 2
QEMU_NET_OPT    = -netdev tap,id=mytap,ifname=tap0,script=${DIR_QEMU}/etc/qemu-ifup,downscript=${DIR_QEMU}/etc/qemu-ifdown \
                    -device e1000,netdev=mytap

QEMU_RECORD     = -icount shift=0,rr=record,rrfile=.qemu-replay.bin
QEMU_REPLAY     = -icount shift=0,rr=replay,rrfile=.qemu-replay.bin

# -----------------------------------------------------------------------
# UCAS-OS Entrypoints and Variables
# -----------------------------------------------------------------------

DIR_ARCH        = ./arch/riscv
DIR_BUILD       = ./build
DIR_DRIVERS     = ./drivers
DIR_INIT        = ./init
DIR_KERNEL      = ./kernel
DIR_LIBS        = ./libs
DIR_TINYLIBC    = ./tiny_libc
DIR_TEST        = ./test
DIR_TEST_PROJ   = $(DIR_TEST)/test_project$(PROJECT_IDX)

BOOTLOADER_ENTRYPOINT   = 0x50200000
KERNEL_ENTRYPOINT       = 0xffffffc050202000
USER_ENTRYPOINT         = 0x10000

# -----------------------------------------------------------------------
# UCAS-OS Kernel Source Files
# -----------------------------------------------------------------------

SRC_BOOT    = $(wildcard $(DIR_ARCH)/boot/*.S)
SRC_ARCH    = $(wildcard $(DIR_ARCH)/kernel/*.S)
SRC_BIOS    = $(wildcard $(DIR_ARCH)/bios/*.c)
SRC_START   = $(wildcard $(DIR_ARCH)/kernel/*.c)
SRC_DRIVER  = $(wildcard $(DIR_DRIVERS)/*.c)
SRC_INIT    = $(wildcard $(DIR_INIT)/*.c)
SRC_KERNEL  = $(wildcard $(DIR_KERNEL)/*/*.c)
SRC_LIBS    = $(wildcard $(DIR_LIBS)/*.c)
SRCPP_KERNEL= $(wildcard $(DIR_KERNEL)/*/*.cpp)

SRCPP_MAIN  = $(SRCPP_KERNEL)

SRC_MAIN    = $(SRC_ARCH) $(SRC_START) $(SRC_INIT) $(SRC_BIOS) $(SRC_DRIVER) $(SRC_KERNEL) $(SRC_LIBS)

ELF_BOOT    = $(DIR_BUILD)/bootblock
ELF_MAIN    = $(DIR_BUILD)/main
ELF_IMAGE   = $(DIR_BUILD)/image

# -----------------------------------------------------------------------
# UCAS-OS User Source Files
# -----------------------------------------------------------------------

SRC_CRT0    = $(wildcard $(DIR_ARCH)/crt0/*.S)
OBJ_CRT0    = $(DIR_BUILD)/$(notdir $(SRC_CRT0:.S=.o))

SRC_LIBC    = $(wildcard ./tiny_libc/*.c)
OBJ_LIBC    = $(patsubst %.c, %.o, $(foreach file, $(SRC_LIBC), $(DIR_BUILD)/$(notdir $(file))))
LIB_TINYC   = $(DIR_BUILD)/libtinyc.a

SRC_SHELL	= $(DIR_TEST)/shell.c
SRC_USER    = $(SRC_SHELL) $(wildcard $(DIR_TEST_PROJ)/*.c)
ELF_USER    = $(patsubst %.c, %, $(foreach file, $(SRC_USER), $(DIR_BUILD)/$(notdir $(file))))

# -----------------------------------------------------------------------
# Host Linux Tools Source Files
# -----------------------------------------------------------------------

SRC_CREATEIMAGE = ./tools/createimage.c
ELF_CREATEIMAGE = $(DIR_BUILD)/$(notdir $(SRC_CREATEIMAGE:.c=))

# -----------------------------------------------------------------------
# Top-level Rules
# -----------------------------------------------------------------------

all: dirs elf image asm # floppy

dirs:
	@mkdir -p $(DIR_BUILD)

clean:
	rm -rf $(DIR_BUILD)

floppy:
	sudo dd if=$(DIR_BUILD)/image of=$(DISK)$(DISK_SECTOR) conv=notrunc
	# sudo fdisk -l $(DISK)

asm: $(ELF_BOOT) $(ELF_MAIN) $(ELF_USER)
	for elffile in $^; do $(OBJDUMP) -d $$elffile > $(notdir $$elffile).txt; done

gdb:
	$(GDB) $(ELF_MAIN) -ex "target remote:1234" -s .gdbinit

host-gdb:
	$(HOST_GDB) $(ELF_MAIN) -ex "target remote:1234" -s .gdbinit

host-gdb-boot:
	$(HOST_GDB) $(ELF_BOOT) -ex "target remote:1234" -s .gdbinit


lldb:
	lldb $(ELF_MAIN) -s .lldbinit

run:
	$(QEMU) $(QEMU_OPTS)

run-smp:
	$(QEMU) $(QEMU_OPTS) $(QEMU_SMP_OPT)

run-record:
	$(QEMU) $(QEMU_OPTS) $(QEMU_RECORD)

run-net:
	-@sudo kill `sudo lsof | grep tun | awk '{print $$2}'`
	sudo $(QEMU) $(QEMU_OPTS) $(QEMU_NET_OPT) $(QEMU_SMP_OPT)

debug:
	$(QEMU) $(QEMU_OPTS) $(QEMU_DEBUG_OPT)

debug-smp:
	$(QEMU) $(QEMU_OPTS) $(QEMU_SMP_OPT) $(QEMU_DEBUG_OPT)

debug-record:
	$(QEMU) $(QEMU_OPTS) $(QEMU_DEBUG_OPT) $(QEMU_RECORD)

debug-replay:
	$(QEMU) $(QEMU_OPTS) $(QEMU_DEBUG_OPT) $(QEMU_REPLAY)

debug-net:
	-@sudo kill `sudo lsof | grep tun | awk '{print $$2}'`
	sudo $(QEMU) $(QEMU_OPTS) $(QEMU_DEBUG_OPT) $(QEMU_NET_OPT) $(QEMU_SMP_OPT)

viewlog:
	@if [ ! -e $(QEMU_LOG_FILE) ]; then touch $(QEMU_LOG_FILE); fi;
	@tail -f $(QEMU_LOG_FILE)

minicom:
	sudo $(MINICOM) -D $(TTYUSB1)

.PHONY: all dirs clean floppy asm gdb run debug viewlog minicom
.PHONY: host-gdb lldb
.PHONY: debug-record debug-replay
.PHONY: run-smp debug-smp
.PHONY: run-net debug-net

# -----------------------------------------------------------------------
# UCAS-OS Rules
# -----------------------------------------------------------------------

$(ELF_BOOT): $(SRC_BOOT) riscv.lds
	$(CC) $(BOOT_CFLAGS) -o $@ $(SRC_BOOT) -e main

$(ELF_MAIN): $(SRC_MAIN) $(SRCPP_MAIN) riscv.lds
	$(CC) -r -o $@.c.o $(KERNEL_CFLAGS) $(SRC_MAIN)
	$(CC) -r -o $@.cpp.o $(KERNEL_CXXFLAGS) $(SRCPP_MAIN)
	$(CC) -o $@ $@.c.o $@.cpp.o $(KERNEL_LDFLAGS) -e _boot

$(OBJ_CRT0): $(SRC_CRT0)
	$(CC) $(USER_CFLAGS) -I$(DIR_ARCH)/include -c $< -o $@

$(LIB_TINYC): $(OBJ_LIBC)
	$(AR) rcs $@ $^

$(DIR_BUILD)/%.o: $(DIR_TINYLIBC)/%.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(DIR_BUILD)/%: $(DIR_TEST_PROJ)/%.c $(OBJ_CRT0) $(LIB_TINYC) riscv.lds
	$(CC) $(USER_CFLAGS) -o $@ $(OBJ_CRT0) $< $(USER_LDFLAGS) -Wl,--defsym=TEXT_START=$(USER_ENTRYPOINT) -T riscv.lds

$(DIR_BUILD)/%: $(DIR_TEST)/%.c $(OBJ_CRT0) $(LIB_TINYC) riscv.lds
	$(CC) $(USER_CFLAGS) -o $@ $(OBJ_CRT0) $< $(USER_LDFLAGS) -Wl,--defsym=TEXT_START=$(USER_ENTRYPOINT) -T riscv.lds

elf: $(ELF_BOOT) $(ELF_MAIN) $(LIB_TINYC) $(ELF_USER)

.PHONY: elf

# -----------------------------------------------------------------------
# Host Linux Rules
# -----------------------------------------------------------------------

$(ELF_CREATEIMAGE): $(SRC_CREATEIMAGE)
	$(HOST_CC) $(SRC_CREATEIMAGE) -o $@ -ggdb -Wall

image: $(ELF_CREATEIMAGE) $(ELF_BOOT) $(ELF_MAIN) $(ELF_USER)
	cd $(DIR_BUILD) && ./$(<F) --extended $(filter-out $(<F), $(^F)) && dd if=/dev/zero of=image oflag=append conv=notrunc bs=64MiB count=1

.PHONY: image
