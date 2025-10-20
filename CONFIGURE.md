# 配置说明 (Configuration Guide)

## 概述

本项目提供了一个 `configure` 脚本来自动检测和配置不同平台（Linux 和 macOS）的构建环境。

## 使用方法

### 1. 首次配置

在第一次构建项目之前，运行配置脚本：

```bash
chmod +x configure
./configure
```

脚本会自动：
- 检测当前操作系统平台
- 查找 RISC-V 工具链
- 查找 QEMU 模拟器
- 生成 `config.mk` 配置文件

### 2. 自定义配置

如果需要自定义路径，脚本会提示您输入。您也可以直接编辑生成的 `config.mk` 文件。

### 3. 构建项目

配置完成后，使用标准的 make 命令：

```bash
make all
```

## macOS 平台特定说明

### 安装依赖工具

在 macOS 上，您需要安装以下工具：

1. **RISC-V 工具链**：
   ```bash
   brew tap riscv/riscv
   brew install riscv-tools
   ```
   
   或者：
   ```bash
   brew install riscv64-elf-gcc
   ```

2. **QEMU**：
   ```bash
   brew install qemu
   ```

3. **GNU 工具** (可选，但推荐)：
   ```bash
   brew install coreutils gnu-sed
   ```

### 工具链差异

macOS 上的 RISC-V 工具链可能使用不同的前缀：
- `riscv64-unknown-elf-` (Homebrew 常见)
- `riscv64-elf-`
- `riscv64-unknown-linux-gnu-`

配置脚本会自动检测可用的工具链。

### 设备路径差异

- Linux: `/dev/sdb`
- macOS: `/dev/disk2` 或 `/dev/disk3`

使用 `diskutil list` 查看 macOS 上的磁盘设备。

## Linux 平台说明

在 Linux 上，默认配置应该可以直接使用。如果需要安装工具链：

```bash
# Ubuntu/Debian
sudo apt-get install gcc-riscv64-unknown-elf qemu-system-misc

# 或从源码编译 RISC-V 工具链
git clone https://github.com/riscv/riscv-gnu-toolchain
```

## 配置文件说明

`config.mk` 文件包含以下关键变量：

- `PLATFORM`: 操作系统平台 (Darwin/Linux)
- `CROSS_PREFIX`: 交叉编译工具链前缀
- `DIR_OSLAB`: OSLab 主目录
- `DIR_QEMU`: QEMU 安装目录
- `DISK`: 磁盘设备路径
- `TTYUSB1`: 串口设备路径

## 重新配置

如果需要更改配置，只需重新运行：

```bash
./configure
```

这会覆盖现有的 `config.mk` 文件。

## 故障排除

### 找不到工具链

如果配置脚本找不到 RISC-V 工具链：

1. 确认已正确安装工具链
2. 检查工具链是否在 PATH 中：
   ```bash
   which riscv64-unknown-elf-gcc
   ```
3. 手动编辑 `config.mk` 设置正确的 `CROSS_PREFIX`

### QEMU 版本问题

某些 QEMU 版本可能不支持所有功能。推荐使用 QEMU 5.0 或更高版本。

### macOS 上的 GNU 工具

如果遇到 sed/grep 相关错误，安装 GNU 版本：
```bash
brew install gnu-sed
```

然后在 `config.mk` 中使用 `gsed` 而不是 `sed`。

## 清理配置

要清理生成的配置文件：

```bash
rm -f config.mk
```

然后重新运行 `./configure`。
