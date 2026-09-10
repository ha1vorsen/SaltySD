@ BGM override. Place at 0xA36B00 and reach it with a bl from the point in the
@ game's BGM path builder where the track name has just been resolved.
@
@ That function tries rex:, then rom:/patch, then rom:. Asking the payload
@ instead of overwriting the first base pointer leaves all three stock tiers
@ intact, because a path is only written when a mod actually has the track.
@
@ Entered with r5 = the caller's struct (buffer at +4), r6 = the track name, and
@ lr = the instruction after the hook, so nothing here needs an address that
@ moves between versions.

.arm

.equ saltysd_build_named_path,  0xA33008
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

    @ A mod has this track and the path is already written, so answer 1 and
    @ return straight out of the game's function. Its epilogue, replayed: the
    @ stock search never runs and no handle was opened to close.
    mov r0, #0x1
    add sp, sp, #0x8
    pop {r4,r5,r6,pc}

no_override:
    @ Replay the displaced instruction and rejoin the stock search.
    mov r3, r6
    bx lr

.pool
