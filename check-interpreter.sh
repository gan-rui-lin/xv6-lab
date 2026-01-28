#!/bin/bash
# claude: 检查测试程序的解释器路径

docker run --rm --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  ubuntu:22.04 bash << 'EOF'
apt-get update > /dev/null 2>&1
apt-get install -y e2fsprogs binutils file > /dev/null 2>&1
losetup -f /image.img
mount -o ro /dev/loop0 /mnt

echo "=== glibc pipe interpreter ==="
file /mnt/glibc/pipe
readelf -l /mnt/glibc/pipe 2>/dev/null | grep -A 1 "Requesting program interpreter"

echo ""
echo "=== musl pipe interpreter ==="
file /mnt/musl/pipe
readelf -l /mnt/musl/pipe 2>/dev/null | grep -A 1 "Requesting program interpreter"

echo ""
echo "=== /lib directory ==="
ls -la /mnt/lib/

echo ""
echo "=== glibc/lib directory ==="
ls -la /mnt/glibc/lib/ | grep ld-

umount /mnt
losetup -d /dev/loop0
EOF
