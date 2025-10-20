# Auto-generated configuration file
# Platform: Linux
# Generated: Mon Oct 20 21:22:59 CST 2025

# -----------------------------------------------------------------------
# Platform-specific Configuration
# -----------------------------------------------------------------------

PLATFORM        = Linux
CROSS_PREFIX    = riscv64-unknown-linux-gnu-
DIR_OSLAB       = /home/ycp/OSLab-RISC-V
DIR_QEMU        = /home/ycp/OSLab-RISC-V/qemu
DISK            = /dev/sdb
TTYUSB1         = /dev/ttyUSB1

# Derived paths
DIR_UBOOT       = $(DIR_OSLAB)/u-boot

# Linux-specific settings
QEMU            = $(DIR_QEMU)/riscv64-softmmu/qemu-system-riscv64
MINICOM         = minicom

