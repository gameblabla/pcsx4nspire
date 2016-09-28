@ vim:filetype=armasm


.global sys_cacheflush @ void *start_addr, void *end_addr

sys_cacheflush:
    mov     r2, #0
    swi     __ARM_NR_cacheflush
    bx      lr

.global spend_cycles
spend_cycles:
    mov r0,r0,lsr #2
  0:subs r0, r0, #1
    bne 0b
    bx lr

