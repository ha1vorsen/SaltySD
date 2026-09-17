
.arm

.equ saltysd_build_named_path,  0x07000114
.equ SALTYSD_KIND_BGM,          0x1
.equ BGM_PATH_SIZE,             0x100

main:
    push {r0-r3,r12,lr}
        add r0, r5, #0x4
        mov r1, #BGM_PATH_SIZE
        mov r2, #SALTYSD_KIND_BGM
        mov r3, r6
        ldr r12, =saltysd_build_named_path
        blx r12
        cmp r0, #0x0
    pop {r0-r3,r12,lr}
    beq no_override

    mov r0, #0x1
    add sp, sp, #0x8
    pop {r4,r5,r6,pc}

no_override:
    @ Replay the displaced instruction and rejoin the stock search.
    mov r3, r6
    bx lr

.pool
