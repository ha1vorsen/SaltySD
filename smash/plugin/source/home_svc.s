.arm

.global get_thread_tls
.type get_thread_tls, %function
get_thread_tls:
    mrc     p15, 0, r0, c13, c0, 3
    bx      lr

.global saltysd_svc_create_thread
.type saltysd_svc_create_thread, %function
saltysd_svc_create_thread:
    svc     0x08
    bx      lr

.global saltysd_svc_sleep
.type saltysd_svc_sleep, %function
saltysd_svc_sleep:
    svc     0x0A
    bx      lr

.global saltysd_svc_arbitrate
.type saltysd_svc_arbitrate, %function
saltysd_svc_arbitrate:
    svc     0x22
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
