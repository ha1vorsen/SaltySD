@ Calls the plugin's thread loader entry, which a bl cannot reach.

.arm

lock_hook:
    ldr r4, =0x07000108
    blx r4

.pool
