	cpu dcj-11
	org 0
	dw 0

	org 024
	dw start
	dw 0

source_addr equ 0600
result_base equ 0620
done_addr equ 0640
io_base equ 0177000

	org 0100
start
	mov #source_addr, r5

	; Mode 1: register deferred.
	mov #io_base+0, r0
	movb (r5), (r0)

	; Mode 2: byte autoincrement is one for R0-R5.
	mov #io_base+1, r0
	movb (r5), (r0)+
	mov r0, @#result_base+0

	; Mode 3: deferred autoincrement consumes a word pointer.
	mov #pointer_mode3, r0
	movb (r5), @(r0)+
	mov r0, @#result_base+2

	; Mode 4: byte autodecrement is one for R0-R5.
	mov #io_base+4, r0
	movb (r5), -(r0)
	mov r0, @#result_base+4

	; Mode 5: deferred autodecrement consumes a word pointer.
	mov #pointer_mode5+2, r0
	movb (r5), @-(r0)
	mov r0, @#result_base+6

	; Modes 6 and 7: index word and deferred pointer reads are required, but
	; the final device registers must not be read.
	mov #io_base-6+5, r0
	movb (r5), 6(r0)
	mov #pointer_mode7-4, r0
	movb (r5), @4(r0)

	; SP keeps the architectural two-byte step for byte operations.
	mov #io_base+7, sp
	movb (r5), (sp)+
	mov sp, @#result_base+10
	mov #io_base+12, sp
	movb (r5), -(sp)
	mov sp, @#result_base+12

	mov #1, @#done_addr
passed
	br passed

	org 0500
pointer_mode3
	dw io_base+2
pointer_mode5
	dw io_base+4
pointer_mode7
	dw io_base+6

	org source_addr
	db 0123, 0

	org result_base
	ds 16

	org done_addr
	dw 0
