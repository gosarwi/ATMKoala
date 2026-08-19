.section .text
.global _start
.type _start,@function
.extern __atm_libc_start_main
_start:
    xorq %rbp,%rbp
    movq %rsp,%rdi
    andq $-16,%rsp
    call __atm_libc_start_main
    hlt
.size _start,.-_start
.section .note.GNU-stack,"",@progbits
