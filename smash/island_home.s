.arm

@ Resident gate; plugin unmapped under HOME Menu

.equ home_gate_marker,       0x11111111
.equ normal_continue_marker, 0x22222222
.equ normal_skip_marker,     0x33333333

.global saltysd_home_normal_entry

saltysd_home_normal_entry:
    push    {r3}
    ldr     r12, =home_gate_marker
1:  ldrex   r3, [r12]
    tst     r3, #0x80000000
    bne     2f
    add     r3, r3, #1
    strex   r2, r3, [r12]
    cmp     r2, #0
    bne     1b
    adr     lr, saltysd_home_normal_exit - 4
    ldr     r12, =0x0700010C
    pop     {r3}
    bx      r12

@ Plugin unavailable; stock continuation
2:  mov     r2, r5
    mov     r1, r7
    mov     r0, r6
    pop     {r3}
    ldr     pc, =normal_continue_marker

@ Fixed return offsets from normload.s
saltysd_home_normal_exit:
    ldr     r12, =normal_continue_marker
    b       saltysd_home_normal_leave
    nop
    nop
saltysd_home_normal_skip:
    ldr     r12, =normal_skip_marker
    b       saltysd_home_normal_leave

saltysd_home_normal_leave:
    push    {r0-r3}
    ldr     r3, =home_gate_marker
3:  ldrex   r2, [r3]
    sub     r2, r2, #1
    strex   r1, r2, [r3]
    cmp     r1, #0
    bne     3b
    pop     {r0-r3}
    bx      r12

.pool
