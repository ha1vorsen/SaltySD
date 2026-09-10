.section ".init"
.arm
.align 4
.global _start

@   0xA33008  named path builder, called from cro_redir.asm and sdbgm.asm
_start:
    b _rf_init
    b saltysd_build_prefix
    b saltysd_build_named_path

_rf_init:
    add lr, lr, #0x4
    
    push {r0-r12,lr}
	blx _main
	pop {r0-r12,lr}
	
	@accomodate for replaced code
	ldrb r1, [r0, #0x2]
	cmp r1, #0x4
	addcc lr, lr, #0x18
	
	bx lr
