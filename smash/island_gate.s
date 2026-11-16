.arm

.equ home_gate_marker,      0x11111111
.equ rf_continue_marker,    0x55555555
.equ rf_skip_marker,        0x56565656
.equ menu_continue_marker,  0x66666666

.equ plugin_rf_hook,        0x07000104
.equ plugin_named_path,     0x07000114
.equ plugin_cro_loaded,     0x07000118
.equ plugin_menu_hook,      0x0700011C

.equ GATE_LOCK,             0x80000000
.equ GATE_OFF,              0x40000000
.equ SE_HOST_HEADER_SIZE,   0x14
.equ SE_HOST_ALLOC,         0x0C

saltysd_gated:
    b       saltysd_gate_enter
    b       saltysd_gate_wait
    b       saltysd_gate_leave
    b       saltysd_rf_relay
    b       saltysd_menu_relay
    b       saltysd_menu_exit
    b       saltysd_se_get_host
    b       saltysd_named_path_skip
    b       saltysd_named_path_wait
    b       saltysd_cro_mapped_relay
se_host_real:
    .word   0
se_host_table:
    .word   0, 0, 0, 0, 0
    .word   se_host_log
    .word   se_host_get_build_identity
    .word   se_host_resolve_target
    .word   se_host_alloc
    .word   se_host_free
    .word   se_host_open_read
    .word   se_host_read
    .word   se_host_close
    .word   se_host_emit_diagnostic
    .word   se_host_install_hook
    .word   se_host_remove_hook

saltysd_gate_enter:
    push    {r0-r2, lr}
    ldr     r0, =home_gate_marker
1:  ldrex   r1, [r0]
    tst     r1, #GATE_LOCK
    bne     2f
    add     r1, r1, #1
    strex   r2, r1, [r0]
    cmp     r2, #0
    bne     1b
2:  pop     {r0-r2, pc}

saltysd_gate_wait:
    push    {r0-r3, r12, lr}
1:  bl      saltysd_gate_enter
    beq     2f
    ldr     r0, =home_gate_marker
    ldr     r0, [r0]
    tst     r0, #GATE_OFF
    bne     2f
    ldr     r0, =1000000
    mov     r1, #0
    svc     0x0A
    b       1b
2:  pop     {r0-r3, r12, pc}

saltysd_gate_leave:
    push    {r0-r3}
    mrs     r3, CPSR
    ldr     r0, =home_gate_marker
1:  ldrex   r1, [r0]
    sub     r1, r1, #1
    strex   r2, r1, [r0]
    cmp     r2, #0
    bne     1b
    msr     CPSR_f, r3
    pop     {r0-r3}
    bx      lr

saltysd_rf_relay:
    push    {r2, lr}
    bl      saltysd_gate_enter
    bne     rf_closed
    ldr     r2, =plugin_rf_hook
rf_call:
    blx     r2
    nop
rf_open_continue:
    ldr     r2, =rf_continue_marker
    b       rf_leave
    nop
    nop
    nop
    nop
rf_open_skip:
    ldr     r2, =rf_skip_marker
rf_leave:
    bl      saltysd_gate_leave
    b       rf_out
rf_closed:
    ldrb    r1, [r0, #0x2]
    cmp     r1, #0x4
    ldrcs   r2, =rf_continue_marker
    ldrcc   r2, =rf_skip_marker
rf_out:
    ldr     lr, [sp, #4]
    add     sp, sp, #8
    bx      r2

.if (rf_open_continue - rf_call) != 0x8
.error "rf relay normal return offset changed"
.endif
.if (rf_open_skip - rf_call) != 0x20
.error "rf relay skip return offset changed"
.endif

saltysd_menu_relay:
    push    {r2, lr}
    bl      saltysd_gate_enter
    pop     {r2, lr}
    ldreq   pc, =plugin_menu_hook
    ldr     r2, [r0, #0x40]
    ldr     pc, =menu_continue_marker

saltysd_menu_exit:
    push    {r12, lr}
    bl      saltysd_gate_leave
    pop     {r12, lr}
    ldr     pc, =menu_continue_marker

saltysd_se_get_host:
    cmp     r0, #1
    adreq   r0, se_host_table
    movne   r0, #0
    bx      lr

saltysd_named_path_skip:
    push    {r4, lr}
    bl      saltysd_gate_enter
    bne     named_path_skip_closed
    ldr     r12, =plugin_named_path
    blx     r12
    bl      saltysd_gate_leave
    pop     {r4, pc}
named_path_skip_closed:
    mov     r0, #0
    pop     {r4, pc}

saltysd_named_path_wait:
    push    {r4, lr}
    bl      saltysd_gate_wait
    bne     named_path_wait_closed
    ldr     r12, =plugin_named_path
    blx     r12
    bl      saltysd_gate_leave
    pop     {r4, pc}
named_path_wait_closed:
    mov     r0, #0
    pop     {r4, pc}

saltysd_cro_mapped_relay:
    push    {r0-r3, r12, lr}
    bl      saltysd_gate_wait
    bne     1f
    ldr     r12, =plugin_cro_loaded
    blx     r12
    bl      saltysd_gate_leave
1:  pop     {r0-r3, r12, pc}

se_host_log:                mov r12, #0x00
                            b   se_host_call
se_host_get_build_identity: mov r12, #0x04
                            b   se_host_call
se_host_resolve_target:     mov r12, #0x08
                            b   se_host_call
se_host_alloc:              mov r12, #0x0C
                            b   se_host_call
se_host_free:               mov r12, #0x10
                            b   se_host_call
se_host_open_read:          mov r12, #0x14
                            b   se_host_call
se_host_read:               mov r12, #0x18
                            b   se_host_call
se_host_close:              mov r12, #0x1C
                            b   se_host_call
se_host_emit_diagnostic:    mov r12, #0x20
                            b   se_host_call
se_host_install_hook:       mov r12, #0x24
                            b   se_host_call
se_host_remove_hook:        mov r12, #0x28
                            b   se_host_call

se_host_call:
    push    {r4, lr}
    bl      saltysd_gate_enter
    bne     se_host_closed
    ldr     r4, [sp, #8]
    sub     sp, sp, #8
    str     r4, [sp]
    ldr     r4, se_host_real
    add     r4, r4, #SE_HOST_HEADER_SIZE
    ldr     r12, [r4, r12]
    blx     r12
    add     sp, sp, #8
    bl      saltysd_gate_leave
    pop     {r4, pc}
se_host_closed:
    cmp     r12, #SE_HOST_ALLOC
    moveq   r0, #0
    mvnne   r0, #0
    pop     {r4, pc}

.pool
