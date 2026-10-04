    .syntax unified
    .arm

/* validated host import literal */
    .section .text.se_host_veneer, "ax"
    .align 2
    .global se_get_host_import
    .type se_get_host_import, %function
se_get_host_import:
    ldr ip, [pc]
    bx  ip
    .word 0
    .size se_get_host_import, . - se_get_host_import
