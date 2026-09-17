@ Steps lr past the thunk's literal word and replays the code it displaced.

.arm
.section .text.rf_hook, "ax"

.global saltysd_rf_hook
.type saltysd_rf_hook, %function
saltysd_rf_hook:
    add     lr, lr, #0x4

    push    {r0-r12, lr}
    bl      _main
    pop     {r0-r12, lr}

    ldrb    r1, [r0, #0x2]
    cmp     r1, #0x4
    addcc   lr, lr, #0x18

    bx      lr
