    .syntax unified
    .arm

    .section .cave_blend_mark, "ax"
    .global cave_blend_mark
cave_blend_mark:
ldr     r0, =0xd4b9c0
        ldr     r2, [r0]
        add     r2, r2, #1
        str     r2, [r0]
        str     r4, [r0, #4]
        ldr     r0, =0xd4b960
        mov     r2, #8
    cave_blend_mark_find:
        ldr     r3, [r0]
        cmp     r3, r4
        cmpne   r3, #0
        beq     cave_blend_mark_store
        add     r0, r0, #4
        subs    r2, r2, #1
        bne     cave_blend_mark_find
        b       cave_blend_mark_resume
    cave_blend_mark_store:
        str     r4, [r0]
    cave_blend_mark_resume:
        mov     r2, #0
        mov     r1, #0x10
        b       fall_exit_continue
    .ltorg

    .section .cave_blend_motion, "ax"
    .global cave_blend_motion
cave_blend_motion:
push    {r2, r3, ip, lr}
        ldr     r2, =0xd4b960
        mov     r3, #8
    cave_blend_motion_find:
        ldr     ip, [r2]
        cmp     ip, r0
        beq     cave_blend_motion_pending
        add     r2, r2, #4
        subs    r3, r3, #1
        bne     cave_blend_motion_find
        pop     {r2, r3, ip, lr}
        b       cave_blend_motion_vanilla
    cave_blend_motion_pending:
        ldr     ip, =0xd4b9c0
        ldr     r3, [ip, #8]
        add     r3, r3, #1
        str     r3, [ip, #8]
        str     r0, [ip, #0xC]
        mov     ip, #0
        str     ip, [r2]
        pop     {r2, r3, ip, lr}

        push    {r4, r5, lr}
        mov     r4, r0
        vpush   {d8}
        vmov.f64 d8, d0
        ldr     r0, [r0, #8]
        ldr     r3, [r4, #0x6C]
        sub     sp, sp, #0x1C
        ldr     r5, [r0]
        cmp     r3, #0
        mov     r0, r1
        beq     cave_blend_motion_customized
        ands    r2, r3, #3
        mov     r0, #0
        bic     ip, r3, #3
        beq     cave_blend_motion_custom0
        cmp     r2, #1
        beq     cave_blend_motion_custom1
        cmp     r2, #2
        beq     cave_blend_motion_custom2
        cmp     r2, #3
        bne     cave_blend_motion_customized
        b       cave_blend_motion_custom3
    cave_blend_motion_custom0:
        mov     r0, r4
        blx     ip
        b       cave_blend_motion_customized
    cave_blend_motion_custom1:
        mov     r0, r4
        blx     ip
        b       cave_blend_motion_customized
    cave_blend_motion_custom2:
        mov     r2, r0
        mov     r0, r4
        blx     ip
        b       cave_blend_motion_customized
    cave_blend_motion_custom3:
        mov     r2, r0
        mov     r3, r2
        mov     r0, r4
        blx     ip
    cave_blend_motion_customized:
        mov     r1, #1
        str     r1, [sp]
        str     r0, [sp, #4]
        add     r3, sp, #8
        vstmia  r3, {s16, s17}
        mov     ip, #0
        mov     r2, #4
        add     r1, sp, #0x10
        cmp     r0, #0x2D                    /* observed FallSpecial ID */
        beq     cave_blend_motion_blend_target
        sub     r3, r0, #0x100
        cmp     r3, #3                       /* observed custom ID 0x103 */
        bne     cave_blend_motion_descriptor
    cave_blend_motion_blend_target:
        ldr     r3, =0xd4b9c0
        str     r0, [r3, #0x10]
        ldr     r3, =0x40c00000
        str     r3, [sp, #0x10]
        mov     r2, #5
        add     r1, sp, #0x14
        mov     ip, #1
    cave_blend_motion_descriptor:
        mov     r0, sp
        stm     r1, {r0, r2}
        ldr     r0, [r5, #4]
        add     r0, r0, #0x7000
        ldr     r0, [r0, #0x114]
        cmp     ip, #0
        beq     cave_blend_motion_send
        ldr     r3, =0xd4b9c0
        str     r0, [r3, #0x14]
        ldr     r1, =0xd4b980
        mov     r2, #8
    cave_blend_motion_queue_find:
        ldr     r3, [r1]
        cmp     r3, r0
        cmpne   r3, #0
        beq     cave_blend_motion_queue_store
        add     r1, r1, #8
        subs    r2, r2, #1
        bne     cave_blend_motion_queue_find
        b       cave_blend_motion_queue_done
    cave_blend_motion_queue_store:
        str     r0, [r1]
        ldr     r3, =0x40c00000
        str     r3, [r1, #4]
    cave_blend_motion_queue_done:
        add     r1, sp, #0x14
    cave_blend_motion_send:
        bl      motion_command_send
        vstr    s16, [r4, #0x68]
        add     sp, sp, #0x1C
        mov     r0, #0
        vpop    {d8}
        pop     {r4, r5, pc}
    cave_blend_motion_vanilla:
        push    {r4, r5, lr}
        b       motion_helper_continue
    .ltorg

    .section .cave_blend_force, "ax"
    .global cave_blend_force
cave_blend_force:
push    {r0, r1, r2, r3, lr}
        ldr     r0, =0xd4b9c0
        ldr     r1, [r0, #0x18]
        add     r1, r1, #1
        str     r1, [r0, #0x18]
        ldr     r0, =0xd4b980
        mov     r1, #8
    cave_blend_force_force_find:
        ldr     r2, [r0]
        cmp     r2, r4
        beq     cave_blend_force_force_restore
        add     r0, r0, #8
        subs    r1, r1, #1
        bne     cave_blend_force_force_find
        b       cave_blend_force_force_done
    cave_blend_force_force_restore:
        ldr     r1, =0xd4b9c0
        ldr     r2, [r1, #0x28]
        add     r2, r2, #1
        str     r2, [r1, #0x28]
        vmov    r2, s18
        str     r2, [r1, #0x1C]
        str     r4, [r1, #0x20]
        ldr     r3, [r0, #4]
        vmov    s18, r3
        str     r3, [r1, #0x24]
        mov     r3, #0
        str     r3, [r0]
    cave_blend_force_force_done:
        pop     {r0, r1, r2, r3, lr}
        ldrh    r0, [sl, #6]
        b       blend_gate_continue
    .ltorg

