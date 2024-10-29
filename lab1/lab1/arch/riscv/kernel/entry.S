#ifndef PRINT_ONLY
.section .text.entry

.global trap_s
.extern handler_s
.equ reg_size, 0x8
.align 2

trap_s:
# save caller-saved registers and sepc
  addi sp, sp, -104
  sd ra, 96(sp)

  sd t0, 88(sp)
  sd t1, 80(sp)
  sd t2, 72(sp)

  sd a0, 64(sp)
  sd a1, 56(sp)
  sd a2, 48(sp)
  sd a3, 40(sp)
  sd a4, 32(sp)
  sd a5, 24(sp)
  sd a6, 16(sp)
  sd a7, 8(sp)

  csrr t0, sepc
  sd t0, 0(sp)

# call handler_s(scause)
  csrr a0, scause
  call handler_s
# load sepc and caller-saved registers
  ld t0, 0(sp)
  csrw sepc, t0

  ld a7, 8(sp)
  ld a6, 16(sp)
  ld a5, 24(sp)
  ld a4, 32(sp)
  ld a3, 40(sp)
  ld a2, 48(sp)
  ld a1, 56(sp)
  ld a0, 64(sp)
  
  ld t2, 72(sp)
  ld t1, 80(sp)
  ld t0, 88(sp)

  ld ra, 96(sp)

  addi sp, sp, 104

	sret


#endif