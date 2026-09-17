@ Calls the plugin's RF parse entry, which a bl cannot reach.

.arm

main:
    ldr r2, =0x07000104
    blx r2

.pool
