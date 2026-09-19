.arm
.section .text.menu_hook, "ax"

.global saltysd_menu_hook
.type saltysd_menu_hook, %function
saltysd_menu_hook:
    push    {r0, r2, r3, lr}
    mov     r0, r1
    bl      saltysd_menu_state
    mov     r1, r0
    pop     {r0, r2, r3, lr}

    ldr     r12, =saltysd_menu_orig
    ldr     r12, [r12]
    bx      r12

.pool
