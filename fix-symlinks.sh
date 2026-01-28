#!/bin/bash
#claude: 修复镜像中缺失的 glibc 软链接

set -e

echo "=========================================="
echo "Fixing glibc symlinks in sdcard-rv.img"
echo "=========================================="

docker run --rm --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img" \
  alpine:latest sh -c '
    apk add --no-cache e2fsprogs >/dev/null 2>&1
    losetup -f /image.img
    mount /dev/loop0 /mnt

    echo ""
    echo "BEFORE: /lib directory"
    ls -la /mnt/lib/ 2>/dev/null || echo "/lib not found"

    echo ""
    echo "Creating glibc symlinks..."
    cd /mnt/lib
    ln -sf ../glibc/lib/ld-linux-riscv64-lp64d.so.1 ld-linux-riscv64-lp64d.so.1 2>/dev/null || echo "  (already exists)"
    ln -sf ../glibc/lib/libc.so.6 libc.so.6 2>/dev/null || echo "  (already exists)"

    echo ""
    echo "AFTER: /lib directory"
    ls -la /mnt/lib/

    umount /mnt
    losetup -d /dev/loop0
'

echo ""
echo "✅ Done! Now recompile and test xv6"
