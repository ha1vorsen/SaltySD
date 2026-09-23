#include "../../common.h"

.arm
.section .text.menu_hook, "ax"

.equ STATUS_MENU_SITE, 24
.equ STATE_NOTICES, 0x6F

.global saltysd_menu_hook
.type saltysd_menu_hook, %function
saltysd_menu_hook:
    ldr     r12, =saltysd_status
    ldr     r12, [r12, #STATUS_MENU_SITE]
    cmp     r12, #0
    beq     menu_replay
    add     r12, r12, #4
    cmp     lr, r12
    bne     menu_replay
    cmp     r1, #STATE_NOTICES
    bne     menu_replay

    push    {r0, r2, r3, lr}
    mov     r0, r1
    mov     r1, lr
    bl      saltysd_menu_state
    mov     r1, r0
    pop     {r0, r2, r3, lr}

menu_replay:
    ldr     r2, [r0, #0x40]
    ldr     r12, =(menu_hook_site_ADDR + 0x4)
    bx      r12

.pool
