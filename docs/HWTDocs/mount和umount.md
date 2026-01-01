### #define SYS_umount2 39

* 功能：卸载文件系统；
* 输入：指定卸载目录，卸载参数；
* 返回值：成功返回0，失败返回-1；

```
const char *special, int flags;
int ret = syscall(SYS_umount2, special, flags);
```

#### 测试逻辑和代码
```c
#include "unistd.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

//#define MNTPOINT "./mnt"

static char mntpoint[64] = "./mnt";
static char device[64] = "/dev/vda2";
static const char *fs_type = "vfat";

void test_umount() {
	TEST_START(__func__);

	printf("Mounting dev:%s to %s\n", device, mntpoint);
	int ret = mount(device, mntpoint, fs_type, 0, NULL);
	printf("mount return: %d\n", ret);

	if (ret == 0) {
		ret = umount(mntpoint);
		assert(ret == 0);
		printf("umount success.\nreturn: %d\n", ret);
	}

	TEST_END(__func__);
}

int main(int argc,char *argv[]) {
	if(argc >= 2){
		strcpy(device, argv[1]);
	}

	if(argc >= 3){
		strcpy(mntpoint, argv[2]);
	}

	test_umount();
	return 0;
}
```

```python
from test_base import TestBase
import re

class umount_test(TestBase):
    def __init__(self):
        super().__init__("umount", 5)

    def test(self, data):
        self.assert_ge(len(data), 4)
        # self.assert_equal(data[0], "Mounting dev:/dev/vda2 to ./mnt")
        r = re.findall(r"Mounting dev:(.+) to ./mnt", data[0])
        self.assert_equal(len(r) > 0, True)
        self.assert_equal("mount return: 0", data[1])
        self.assert_equal("umount success.", data[2])
        self.assert_equal("return: 0", data[3])
```
### #define SYS_mount 40

* 功能：挂载文件系统；
* 输入：

- special: 挂载设备；
- dir: 挂载点；
- fstype: 挂载的文件系统类型；
- flags: 挂载参数；
- data: 传递给文件系统的字符串参数，可为NULL；

* 返回值：成功返回0，失败返回-1；

```
const char *special, const char *dir, const char *fstype, unsigned long flags, const void *data;

int ret = syscall(SYS_mount, special, dir, fstype, flags, data);
```

#### 测试逻辑和代码
```c
#include "unistd.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

//#define MNTPOINT "./mnt"

static char mntpoint[64] = "./mnt";
static char device[64] = "/dev/vda2";
static const char *fs_type = "vfat";

void test_mount() {
	TEST_START(__func__);

	printf("Mounting dev:%s to %s\n", device, mntpoint);
	int ret = mount(device, mntpoint, fs_type, 0, NULL);
	printf("mount return: %d\n", ret);
	assert(ret == 0);

	if (ret == 0) {
		printf("mount successfully\n");
		ret = umount(mntpoint);
		printf("umount return: %d\n", ret);
	}

	TEST_END(__func__);
}

int main(int argc,char *argv[]) {
	if(argc >= 2){
		strcpy(device, argv[1]);
	}

	if(argc >= 3){
		strcpy(mntpoint, argv[2]);
	}

	test_mount();
	return 0;
}
```

```python
from test_base import TestBase
import re


class mount_test(TestBase):
    def __init__(self):
        super().__init__("mount", 5)

    def test(self, data):
        self.assert_ge(len(data), 4)
        r = re.findall(r"Mounting dev:(.+) to ./mnt", data[0])
        self.assert_equal(len(r) > 0, True)
        self.assert_equal(data[1], "mount return: 0")
        self.assert_equal(data[2], "mount successfully")
        self.assert_equal(data[3], "umount return: 0")
```
