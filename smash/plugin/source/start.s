@ Luma calls 0x07000100 before the game starts; the slots after it are what the game-side patches call.

.section .crt0, "ax"
.arm
.align 2
.global _start
.type _start, %function

_start:
    b       plugin_start
    b       saltysd_rf_hook
    b       saltysd_threadload
    b       saltysd_normload
    b       saltysd_build_prefix
    b       saltysd_build_named_path

plugin_start:
    push    {r0-r11, lr}

    ldr     r0, =__bss_start__
    ldr     r1, =__bss_end__
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    bl      plugin_main

    pop     {r0-r11, lr}
    bx      lr

.pool
