K=kernel
U=user
SRC=src

# ===== 并行编译配置 =====
# 默认使用所有可用 CPU 核心进行并行编译
NPROC := $(shell nproc)
MAKEFLAGS += -j$(NPROC)
# ! 提交时改为 release 模式
mode ?= release
$(info === Build mode: $(mode) ===)


# ===== 路径定义 =====
SRC_DIRS := boot devs lib mm proc sync syscall trap
BUILD_DIR := build

# ===== 文件收集规则 =====
# 收集 src 目录下各子目录的源文件
SRCS := $(shell find $(SRC) -type f \( -name "*.c" -o -name "*.S" \))

$(info === SRCS collected ===)
$(info $(SRCS))

# 将源文件路径转换为目标文件路径
OBJS := $(patsubst $(SRC)/%.c, $(BUILD_DIR)/%.o, $(filter %.c, $(SRCS)))
OBJS += $(patsubst $(SRC)/%.S, $(BUILD_DIR)/%.o, $(filter %.S, $(SRCS)))

# 设置 entry.o 作为特殊的入口目标文件
ENTRY_OBJ := $(BUILD_DIR)/boot/entry.o
OBJS_NO_ENTRY:= $(filter-out $(ENTRY_OBJ), $(OBJS))
# 保证 mkfs.c 不被编译进内核
OBJS_NO_ENTRY:= $(filter-out $(BUILD_DIR)/mkfs/mkfs.o, $(OBJS_NO_ENTRY))
DEPS := $(OBJS:.o=.d)

# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
TOOLPREFIX = riscv64-unknown-elf-

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

QEMU = qemu-system-riscv64

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

CFLAGS = -Wall  -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I. -I$(SRC)
CFLAGS += -march=rv64gc -mabi=lp64
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)


ifeq ($(mode),debug)
CFLAGS += -DLOG_DEBUG_ENABLE
CFLAGS += -DLOG_INFO_ENABLE
CFLAGS += -DLOG_WARN_ENABLE
CFLAGS += -DLOG_ERROR_ENABLE
endif

# CFLAGS += -DPAGE_TABLE_DEBUG
# CFLAGS += -DTICKER_DEBUG
# CFLAGS += -DCONSOLE_DEBUG
# CFLAGS += -DLOG_DEBUG

# 包含头文件路径：添加各个源代码子目录
INCLUDES := -I$(SRC) $(foreach dir,$(SRC_DIRS),-I$(SRC)/$(dir))

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

LDFLAGS = -z max-page-size=4096

# ===== 创建构建目录 =====
dirs:
	@mkdir -p $(BUILD_DIR)
	@for dir in $(SRC_DIRS); do mkdir -p $(BUILD_DIR)/$$dir; done

# ===== 编译规则 =====
$(BUILD_DIR)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# 特殊处理 initcode.S，使其依赖于 user/initcode
$(BUILD_DIR)/boot/initcode.o: $(SRC)/boot/initcode.S $U/initcode
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$K/kernel: dirs $(ENTRY_OBJ) $(OBJS_NO_ENTRY) $(SRC)/linker/kernel.ld $U/initcode.bin
	@mkdir -p $K
	$(LD) $(LDFLAGS) -T $(SRC)/linker/kernel.ld -o $K/kernel $(ENTRY_OBJ) $(OBJS_NO_ENTRY)
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym


# 编译 initcode.c 为 ELF 文件
$U/initcode.o: $U/initcode.c $U/user.h
	$(CC) $(CFLAGS) -nostdinc -I. -I$(SRC) -MMD -MP -c $U/initcode.c -o $U/initcode.o

# 编译 printf.c 为 ELF 文件
$U/printf.o: $U/printf.c $U/user.h
	$(CC) $(CFLAGS) -I. -I$(SRC) -MMD -MP -c $U/printf.c -o $U/printf.o

# 创建入口点对象文件（确保在开头）
$U/entry.o: $U/entry.S
	$(CC) $(CFLAGS) -I. -I$(SRC) -MMD -MP -c -o $@ $<

# 通用的 user 目录编译规则，避免隐式规则造成工具链/ABI不一致
$U/%.o: $U/%.c
	$(CC) $(CFLAGS) -I. -I$(SRC) -MMD -MP -c $< -o $@

$U/%.o: $U/%.S
	$(CC) $(CFLAGS) -I. -I$(SRC) -MMD -MP -c $< -o $@

# 修改链接顺序
$U/initcode: $U/entry.o $U/initcode.o $U/printf.o $U/ulib.o $U/umalloc.o $U/user-riscv.ld
	$(LD) $(LDFLAGS) -T $U/user-riscv.ld -o $@ \
	    $U/entry.o $U/initcode.o $U/printf.o $U/ulib.o $U/umalloc.o
	$(OBJDUMP) -S $@ > $U/initcode.asm

# 从 ELF 文件生成二进制文件
$U/initcode.bin: $U/initcode
	$(OBJCOPY) -S -O binary $< $@

tags: $(OBJS) _init
	etags *.S *.c

# ===== 磁盘文件系统构建工具 =====
$(SRC)/mkfs/mkfs: $(SRC)/mkfs/mkfs.c $(SRC)/fs/fs.h $(SRC)/param.h
	gcc -Wall -I. -o $(SRC)/mkfs/mkfs $(SRC)/mkfs/mkfs.c -DHOST_TIMEVAL_DEFINED
# 	gcc -Werror -Wall -I. -I$(SRC) -o $(SRC)/mkfs/mkfs $(SRC)/mkfs/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o

ULIB = $U/ulib.o $U/printf.o $U/umalloc.o
# ULIB = $U/ulib.o $U/usys.o $U/printf.o

_%: %.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

UPROGS=\
	$U/_sh \
	$U/_cat \
	$U/_echo \
	$U/_mkdir \
	$U/_ls   \
	
# 额外的 apps 名称（位于 user/apps/<name>）
APP_NAMES := \
	wait \
	fork \
	waitpid \
	gettimeofday \
	open \
	read \
	brk \
	mmap \
	openat \
	fstat \
	getpid \
	getppid \
	exit \
	execve \
	test_echo \
	clone \
	yield \
	
# 从 user/apps/<name>/<name> 复制到 user/_<name>
.PHONY: apps
apps: $(APP_NAMES)

# 复制规则：构建 $(U)/<name> 自 $(U)/apps/<name>
$(U)/%: $(U)/apps/%
	cp $< $@

# ===== 磁盘镜像构建 =====
fs.img: $(SRC)/mkfs/mkfs $(UPROGS) $(addprefix $U/,$(APP_NAMES))
	$(SRC)/mkfs/mkfs fs.img $(UPROGS) $(addprefix $U/,$(APP_NAMES))

-include $(DEPS)

clean: 
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	$K/kernel fs.img \
	$(SRC)/mkfs/mkfs .gdbinit
	rm -f $U/initcode $U/initcode.o $U/initcode.asm $U/initcode.sym $U/initcode.d $U/initcode.bin
	rm -f $U/usys.S $U/usys.o $U/usys.d
	rm -f $U/printf.o $U/printf.d
	rm -f $U/*.o $U/*.d
	rm -rf $(BUILD_DIR)
	rm -f kernel-qemu
	rm -f sbi-qemu
# 	rm -f sdcard.img
	rm -f $(UPROGS)
	rm -f $U/*.d $U/*.asm $U/*.o $U/*.sym $U/_*
	rm -f $(addprefix $U/,$(APP_NAMES))

# try to generate a unique GDB port
# GDBPORT = $(shell expr `id -u` % 5000 + 25000)
GDBPORT = 1234
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 2
endif

QEMUOPTS = -machine virt -bios none -kernel $K/kernel -m 128M -smp $(CPUS) -nographic
# QEMUOPTS += -global virtio-mmio.force-legacy=false
# 磁盘相关的 QEMU 选项
QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

qemu: $K/kernel fs.img
	$(QEMU) $(QEMUOPTS)

# 生成FAT32镜像
FAT32_IMG=test.fat32

$(FAT32_IMG):
	rm -f $(FAT32_IMG)
	@echo "Creating file..."
	dd if=/dev/zero of=$(FAT32_IMG) bs=1M count=100 conv=fsync
	@echo "Formatting file as FAT32..."
	mkfs.fat -F 32 $(FAT32_IMG)    # 有的系统叫mkfs.fat，有的叫mkdosfs，任选其一
	chmod a+rw $(FAT32_IMG)
	@echo "Done: FAT32 image ready."

# 新：QEMU用FAT32镜像
QEMUOPTS_FAT32 = -machine virt -bios none -kernel $(K)/kernel -m 128M -smp $(CPUS) -nographic
QEMUOPTS_FAT32 += -drive file=$(FAT32_IMG),if=none,format=raw,id=xfat
QEMUOPTS_FAT32 += -device virtio-blk-device,drive=xfat,bus=virtio-mmio-bus.0

# 新目标：用FAT32启动
qemu-fat32: $(K)/kernel
	$(QEMU) $(QEMUOPTS_FAT32)

fat32-gdb: $(K)/kernel .gdbinit $(FAT32_IMG)
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS_FAT32) -S $(QEMUGDB)

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: $K/kernel .gdbinit fs.img
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

run: $K/kernel 
# 	直接把 KERNEL_ELF 拷贝到根目录并命名为 kernel-qemu
	cp $K/kernel kernel-qemu
# 	cp sdcard.img-backup sdcard.img
	
# 	cp $(SRC)/bootloader/opensbi-riscv64-generic-fw_dynamic.bin sbi-qemu
# 	cp $(SRC)/bootloader/fw_jump.bin sbi-qemu
#
all:
	$(MAKE) clean
	$(MAKE) run

debug:
	$(MAKE) mode=debug all