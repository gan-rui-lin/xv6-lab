dup2系统调用的测试逻辑

```c
#include "unistd.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

/*
 * 测试通过时应输出：
 * "  from fd 100"
 */
void test_dup2(){
	TEST_START(__func__);
	int fd = dup2(STDOUT, 100);
	assert(fd != -1);
	const char *str = "  from fd 100\n";
	write(100, str, strlen(str));
	TEST_END(__func__);
}

int main(void) {
	test_dup2();
	return 0;
}
```

```python
from test_base import TestBase
import re


class dup2_test(TestBase):
    def __init__(self):
        super().__init__("dup2", 2)

    def test(self, data):
        self.assert_ge(len(data), 1)
        self.assert_equal("  from fd 100", data[0])
```

在 syscall.h 中添加 SYS_dup2 = 22

在 sysfile.c 中实现 sys_dup3：

- 支持 dup2 行为：当 flags=0 且 oldfd==newfd 时，直接返回 newfd
- 保留 dup3 行为：当 flags!=0 且 oldfd==newfd 时，返回错误

在 syscall.c 中注册 SYS_dup2：将 [SYS_dup2] 映射到 sys_dup3

在 param.h 中增加 NOFILE 的值：从 16 增加到 128，以支持更大的文件描述符编号