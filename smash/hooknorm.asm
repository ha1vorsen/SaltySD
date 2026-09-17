@ Calls the plugin's normal loader entry, which a bl cannot reach.

.arm

lock_hook:
    ldr r2, =0x0700010C
    blx r2

.pool
