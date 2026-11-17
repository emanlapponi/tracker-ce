@ Word/burst memcpy, memmove, memset, and memcmp in zero-wait-state ITCM for i.MX RT1051 (Tracker+ / Mini).
@
@ platforms/imx links against newlib-nano (-specs=nano.specs), whose lib_a-memcpy-stub.o was
@ already placed in SRAM_ITC on main, while memmove, memset, and memcmp ran as byte loops in
@ QSPI Flash. These four symbols override libc_nano.a's implementations firmware-wide and are
@ placed in .fastmem_ram_functions (linked at the end of .data_RAM2 / SRAM_ITC via
@ platforms/imx/linker/linkerScript.ld, away from address 0x00000008).

	.syntax unified
	.cpu	cortex-m7
	.thumb
	.section .fastmem_ram_functions, "ax", %progbits

@ void* memcpy(void* dst, const void* src, size_t len)
	.global	memcpy
	.type	memcpy, %function
	.thumb_func
	.p2align 2
memcpy:
	cmp	r2, #0
	beq	.Lcp_ret
	mov	r3, r0			@ working dst; preserve r0 return value
	cmp	r2, #4
	blo	.Lcp_bytes
	tst	r3, #3
	beq	.Lcp_dst_aligned
.Lcp_align_dst:
	ldrb	r12, [r1], #1
	strb	r12, [r3], #1
	subs	r2, r2, #1
	tst	r3, #3
	bne	.Lcp_align_dst
.Lcp_dst_aligned:
	tst	r1, #3
	bne	.Lcp_src_unaligned
	cmp	r2, #32
	blo	.Lcp_w4
	push	{r4-r11}
.Lcp_w32:
	ldmia	r1!, {r4-r11}
	stmia	r3!, {r4-r11}
	sub	r2, r2, #32
	cmp	r2, #32
	bhs	.Lcp_w32
	pop	{r4-r11}
.Lcp_w4:
	cmp	r2, #4
	blo	.Lcp_bytes
.Lcp_w4_loop:
	ldr	r12, [r1], #4
	str	r12, [r3], #4
	sub	r2, r2, #4
	cmp	r2, #4
	bhs	.Lcp_w4_loop
	b	.Lcp_bytes
.Lcp_src_unaligned:
	@ r3 is 4-byte aligned; r1 is not. Single ldr is unaligned-safe on Cortex-M7 Normal memory,
	@ whereas ldmia always faults on unaligned addresses.
	cmp	r2, #16
	blo	.Lcp_w4
	push	{r4-r7}
.Lcp_u16:
	ldr	r4, [r1], #4
	ldr	r5, [r1], #4
	ldr	r6, [r1], #4
	ldr	r7, [r1], #4
	stmia	r3!, {r4-r7}
	sub	r2, r2, #16
	cmp	r2, #16
	bhs	.Lcp_u16
	pop	{r4-r7}
	b	.Lcp_w4
.Lcp_bytes:
	cbz	r2, .Lcp_ret
.Lcp_b_loop:
	ldrb	r12, [r1], #1
	strb	r12, [r3], #1
	subs	r2, r2, #1
	bne	.Lcp_b_loop
.Lcp_ret:
	bx	lr
	.size	memcpy, . - memcpy

@ void* memmove(void* dst, const void* src, size_t len)
	.global	memmove
	.type	memmove, %function
	.thumb_func
	.p2align 2
memmove:
	cmp	r2, #0
	beq	.Lmv_ret
	subs	r3, r0, r1
	beq	.Lmv_ret		@ dst == src: nothing to copy
	cmp	r3, r2
	bhs	memcpy			@ dst < src or dst >= src + len: forward copy is overlap-safe
	add	r3, r0, r2		@ working dst end; preserve r0 return value
	add	r1, r1, r2		@ working src end
	cmp	r2, #4
	blo	.Lmv_bytes
	tst	r3, #3
	beq	.Lmv_dst_aligned
.Lmv_align_dst:
	ldrb	r12, [r1, #-1]!
	strb	r12, [r3, #-1]!
	subs	r2, r2, #1
	tst	r3, #3
	bne	.Lmv_align_dst
.Lmv_dst_aligned:
	tst	r1, #3
	bne	.Lmv_w4
	cmp	r2, #32
	blo	.Lmv_w4
	push	{r4-r11}
.Lmv_w32:
	ldmdb	r1!, {r4-r11}
	stmdb	r3!, {r4-r11}
	sub	r2, r2, #32
	cmp	r2, #32
	bhs	.Lmv_w32
	pop	{r4-r11}
.Lmv_w4:
	cmp	r2, #4
	blo	.Lmv_bytes
.Lmv_w4_loop:
	ldr	r12, [r1, #-4]!
	str	r12, [r3, #-4]!
	sub	r2, r2, #4
	cmp	r2, #4
	bhs	.Lmv_w4_loop
.Lmv_bytes:
	cbz	r2, .Lmv_ret
.Lmv_b_loop:
	ldrb	r12, [r1, #-1]!
	strb	r12, [r3, #-1]!
	subs	r2, r2, #1
	bne	.Lmv_b_loop
.Lmv_ret:
	bx	lr
	.size	memmove, . - memmove

@ void* memset(void* dst, int val, size_t len)
	.global	memset
	.type	memset, %function
	.thumb_func
	.p2align 2
memset:
	cmp	r2, #0
	beq	.Lms_ret
	mov	r3, r0			@ working dst; preserve r0 return value
	uxtb	r1, r1
	orr	r1, r1, r1, lsl #8
	orr	r1, r1, r1, lsl #16	@ fill byte broadcast to all 4 lanes
	cmp	r2, #4
	blo	.Lms_bytes
	tst	r3, #3
	beq	.Lms_aligned
.Lms_align_dst:
	strb	r1, [r3], #1
	subs	r2, r2, #1
	tst	r3, #3
	bne	.Lms_align_dst
.Lms_aligned:
	cmp	r2, #32
	blo	.Lms_w4
	push	{r4-r9}
	mov	r4, r1
	mov	r5, r1
	mov	r6, r1
	mov	r7, r1
	mov	r8, r1
	mov	r9, r1
	mov	r12, r1
.Lms_w32:
	stmia	r3!, {r1, r4-r9, r12}
	sub	r2, r2, #32
	cmp	r2, #32
	bhs	.Lms_w32
	pop	{r4-r9}
.Lms_w4:
	cmp	r2, #4
	blo	.Lms_bytes
.Lms_w4_loop:
	str	r1, [r3], #4
	sub	r2, r2, #4
	cmp	r2, #4
	bhs	.Lms_w4_loop
.Lms_bytes:
	cbz	r2, .Lms_ret
.Lms_b_loop:
	strb	r1, [r3], #1
	subs	r2, r2, #1
	bne	.Lms_b_loop
.Lms_ret:
	bx	lr
	.size	memset, . - memset

@ int memcmp(const void* s1, const void* s2, size_t len)
	.global	memcmp
	.type	memcmp, %function
	.thumb_func
	.p2align 2
memcmp:
	cmp	r2, #0
	beq	.Lmc_zero
	cmp	r0, r1
	beq	.Lmc_zero
	cmp	r2, #4
	blo	.Lmc_bytes
.Lmc_w4_loop:
	ldr	r3, [r0], #4
	ldr	r12, [r1], #4
	cmp	r3, r12
	bne	.Lmc_word_diff
	sub	r2, r2, #4
	cmp	r2, #4
	bhs	.Lmc_w4_loop
	b	.Lmc_bytes
.Lmc_word_diff:
	sub	r0, r0, #4
	sub	r1, r1, #4
	movs	r2, #4
.Lmc_bytes:
	cbz	r2, .Lmc_zero
.Lmc_b_loop:
	ldrb	r3, [r0], #1
	ldrb	r12, [r1], #1
	subs	r3, r3, r12
	bne	.Lmc_diff
	subs	r2, r2, #1
	bne	.Lmc_b_loop
.Lmc_zero:
	movs	r0, #0
	bx	lr
.Lmc_diff:
	mov	r0, r3
	bx	lr
	.size	memcmp, . - memcmp
