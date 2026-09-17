@ Reached from the thunk over the thread hook site.

.arm

.macro call func
    ldr r6, =\func
    blx r6
.endm

.include "common.asm"
.equ base_addr,     0x0

.equ saltysd_build_prefix,  0x07000110

.equ TO_LOAD, 0x0
.equ RESOURCE_ID, 0x4
.equ FILE_SIZE, 0x8
.equ BYTES_READ, 0xC
.equ PATH, 0x10

.section .text.threadload, "ax"
.global saltysd_threadload
.type saltysd_threadload, %function
saltysd_threadload:
     @Compensate for removing code
     sub sp, sp, #0x8
     mov r6, r0
     ldr r0, [r2, #0x4]
     
     @ Check RF flags
     push {r0-r6,lr}
        call get_rf_struct
        ldr r0, [r0, #0x8]
        tst r0, #0x8000 @ does this file have an SD override?
     pop {r0-r6,lr}
     bne exit
     
     push {r0-r8,lr}
         sub sp, sp, #0x20
         str r1, [sp, #TO_LOAD] @ Stash to-load address
         str r2, [sp, #RESOURCE_ID]
         
         ldrh r5, [r2]

         ldr r0, =0x404
         call liballoc
         mov r8, r0

         add r0, r8, #0x20
         mov r1, r5
         call saltysd_build_prefix
         cmp r0, #0x0
         beq close
         @ path_str clears 0x104 bytes with STM; start the path on a word
         rsb r1, r0, #0x0
         and r1, r1, #0x3
         add r1, r1, #0x20
         add r1, r8, r1
         str r1, [sp, #PATH]
         add r7, r1, r0
         
         ldr r1, [sp, #RESOURCE_ID]
         mov r0, r7
         sub r0, r0, #0x4
         call path_str
                  
         ldr r0, [sp, #PATH]
         mov r1, r5
         call saltysd_build_prefix
         ldr r7, [sp, #PATH]
               
         mov r0, r8
         call IFile_Init
         
         ldr r0, =sdmc+base_addr
         call mount_sdmc
         
         mov r0, r8
         mov r1, r7
         mov r2, #0x1
         call IFile_Open
         cmp r0, #0x0
         beq close_and_end @ SD file doesn't exist, exit and pretend it never happened.
         
         mov r0, r8
         call IFile_GetSize
         str r0, [sp, #FILE_SIZE]
         
         ldr r1, [sp, #TO_LOAD] @dst
         ldr r2, [sp, #FILE_SIZE] @size
         add r3, sp, #BYTES_READ @bytes_read
         mov r0, r8 @file
         call IFile_Read
end_read_sd:         
         mov r0, r8
         call IFile_Close 
         mov r0, r8
         call libdealloc
         add sp, sp, #0x20
     pop  {r0-r8,lr}
     
     b skip
     
close_and_end:
        mov r0, r8
        call IFile_Close     
close:
     mov r0, r8
     call libdealloc
     add sp, sp, #0x20
     pop  {r0-r8,lr}
     
exit:
     add lr, lr, #0x4
     bx lr
     
skip:
    add sp, sp, #0x8
    pop {r4-r8,lr}
    
skip_end:
    bx lr
    
.pool

.align 4
sdmc:       .asciz "sdmc:"
.align 4
sdmc_:      .asciz "sdmc"
