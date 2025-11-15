# user/entry.S
.section .text.entry
.globl _start
_start:
    
    # 调用 main 函数
    call main
    
    # 如果 main 函数返回，进入死循环
spin: 
    j spin