.arm

.global get_thread_tls
.type get_thread_tls, %function
get_thread_tls:
    mrc     p15, 0, r0, c13, c0, 3
    bx      lr

.global saltysd_svc_create_thread
.type saltysd_svc_create_thread, %function
saltysd_svc_create_thread:
    push    {r0, r4}
    ldr     r0, [sp, #8]
    ldr     r4, [sp, #12]
    svc     0x08
    ldr     r2, [sp], #4
    str     r1, [r2]
    ldr     r4, [sp], #4
    bx      lr

.global saltysd_svc_exit_thread
.type saltysd_svc_exit_thread, %function
saltysd_svc_exit_thread:
    svc     0x09
    bx      lr

.global saltysd_svc_sleep
.type saltysd_svc_sleep, %function
saltysd_svc_sleep:
    svc     0x0A
    bx      lr

.global saltysd_svc_arbitrate
.type saltysd_svc_arbitrate, %function
saltysd_svc_arbitrate:
    push    {r4, r5}
    ldr     r4, [sp, #8]
    ldr     r5, [sp, #12]
    svc     0x22
    pop     {r4, r5}
    bx      lr

@ ARMv6K exclusives; preemption-safe admission
.global saltysd_gate_try_lock
.type saltysd_gate_try_lock, %function
saltysd_gate_try_lock:
1:  ldrex   r1, [r0]
    tst     r1, #0x80000000
    bne     2f
    orr     r2, r1, #0x80000000
    strex   r3, r2, [r0]
    cmp     r3, #0
    bne     1b
    mov     r0, #1
    bx      lr
2:  mov     r0, #0
    bx      lr
