.section ".init"
.arm
.align 4
.global _start

@ The other payloads are patched into their own holes in code.bin and can only
@ reach this one by address, so the head of the payload is a fixed branch table.
@ Both slots have to stay where they are:
@   0xA33000  RF init, called from resource_mod/hookresource.asm
@   0xA33004  prefix builder, called from normload.asm and threadload.asm
_start:
    b _rf_init
    b saltysd_build_prefix

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
