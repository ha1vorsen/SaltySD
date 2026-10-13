.arm

.equ home_gate_marker,        0x11111111
.equ thread_continue_marker,  0x44444444

.global saltysd_home_thread_entry

saltysd_home_thread_entry:
    push    {r2, r3}
    ldr     r12, =home_gate_marker
1:  ldrex   r3, [r12]
    tst     r3, #0x80000000
    bne     2f
    add     r3, r3, #1
    strex   r2, r3, [r12]
    cmp     r2, #0
    bne     1b
    adr     lr, saltysd_home_thread_exit - 4
    ldr     r12, =0x07000108
    pop     {r2, r3}
    bx      r12

2:  pop     {r2, r3}
    sub     sp, sp, #0x8
    mov     r6, r0
    ldr     r0, [r2, #0x4]
    ldr     pc, =thread_continue_marker

saltysd_home_thread_exit:
    cmp     r12, #0
    ldreq   r12, =thread_continue_marker
    adrne   r12, saltysd_home_thread_unwind
    b       saltysd_home_thread_leave

saltysd_home_thread_leave:
    push    {r0-r3}
    ldr     r3, =home_gate_marker
3:  ldrex   r2, [r3]
    sub     r2, r2, #1
    strex   r1, r2, [r3]
    cmp     r1, #0
    bne     3b
    pop     {r0-r3}
    bx      r12

saltysd_home_thread_unwind:
    add     sp, sp, #0x8
    pop     {r4-r8, pc}

.pool
