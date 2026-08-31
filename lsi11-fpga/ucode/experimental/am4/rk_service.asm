	cpu dcj-11

rkcs1 equ 0177440
sd_data equ 0177500

	org 0160000
	dw service
	dw 000340

service
	mov r0, -(sp)
	mov r1, -(sp)
	mov r2, -(sp)
	mov r3, -(sp)
	mov r4, -(sp)
	mov r5, -(sp)
	mov #rkcs1, r5
	mov #sd_data, r4
	mov 002(r5), r3
	mov 004(r5), r2

	; DB is private while the service owns the CPU; use it for current LBA.
	mov 006(r5), r0
	mov r0, -(sp)
	; DA head bits become the low three bits after SWAB; this is equivalent to
	; ASH #-8 here and saves one private-service word.
	swab r0
	bic #0177770, r0
	mul #000026, r0
	mov (sp)+, r0
	bic #0177740, r0
	add r0, r1
	mov r1, -(sp)
	mov 020(r5), r0
	mul #000102, r0
	add (sp)+, r1
	mov r1, 022(r5)

next_sector
	; The bootstrap and every completed sector leave fast mode with CS high (3).
	dec 002(r4)
	; RK611 READ and WRITE differ in CS1 bit 1.  Use it as a word-table index
	; for the corresponding SD CMD17/CMD24 command byte.
	mov (r5), r0
	bic #0177775, r0
	mov command_bytes(r0), (r4)
	clr (r4)
	clr (r4)
	mov 022(r5), r1
	mov r1, r0
	swab r0
	mov r0, (r4)
	mov r1, (r4)
	mov #1, (r4)
	mov #000020, r0
response
	tst (r4)
	beq response_done
	sob r0, response
	br failed
response_done
	bit #2, (r5)
	bne write_sector
wait_token
	clr r0
token
	mov (r4), r1
	cmp #000376, r1
	beq sector_data
	sob r0, token
	br failed

sector_data
	mov #000400, r0
copy_word
	movb (r4), (r2)+
	movb (r4), (r2)+
	inc r3
	dec r0
	beq sector_done
	tst r3
	bne copy_word
discard_word
	tst (r4)
	tst (r4)
	sob r0, discard_word
sector_done
	tst (r4)
	tst (r4)
	inc 002(r4)
	mov #000377, (r4)
	mov r3, 002(r5)
	mov r2, 004(r5)
	inc 022(r5)

	; RK05 geometry is 22 sectors x 3 heads.  Advance DA after every
	; transferred sector, including the last/partial sector of a command.
	incb 006(r5)
	cmpb #000026, 006(r5)
	bne address_done
	; 176752 maps head 2/sector 22 to zero with carry.  For heads 0/1,
	; adding 001400 afterwards maps the invalid sector to the next head.
	add #0176752, 006(r5)
	bcc next_head
	inc 020(r5)
	br address_done
next_head
	add #001400, 006(r5)
address_done
	tst r3
	beq finished
	br next_sector

	; SD writes always transfer one complete 512-byte sector.  RT-11 issues
	; block-sized RK611 transfers, so add a sector to WC after the byte loop.
	; After the accepted-token check, keep CS asserted but stop clocking while
	; the card programs the sector.  Tight back-to-back AM4 polls can leave the
	; physical card busy indefinitely.  The accepted response becomes six
	; below; reuse it as the number of widely spaced, bounded status polls.
write_sector
	mov #000376, (r4)
	; The patched AM4 MOVB path resolves a store-only destination EA, so this
	; memory-to-memory byte transfer does not read and clock SD_DATA first.
	mov #001000, r0
write_byte
	movb (r2)+, (r4)
	sob r0, write_byte
	add #000400, r3
	dec r0				; SOB left zero; use FFFF for both CRC bytes
	mov r0, (r4)
	mov r0, (r4)
write_response
	mov (r4), r1
	incb r1			; FF becomes zero; status 5/13/15 becomes 6/14/16
	beq write_response
	bit #000010, r1		; CRC/write-error responses both set status bit 3
	bne failed
	; SD_DATA is zero-extended by the board register.  Clear only response
	; bits 7:3; bits 7:5 are unspecified and the accepted low status is 6.
	bic #000370, r1
write_busy
	mov r1, r0			; six bounded programming intervals/polls
write_busy_wait
	clr r1				; SOB from zero gives one full 16-bit hold
write_busy_hold
	sob r1, write_busy_hold

write_busy_poll
	tst (r4)
	bne sector_done			; FF is ready; a busy byte is zero
	sob r0, write_busy_wait
	br failed

	; Keep the following high-bit literal at the resource-proven spare-ROM
	; address.  This one-word packing spacer saves three HC1200 slices.
	nop
command_bytes
	dw 000121
	dw 000130
failed
	mov #020000, 014(r5)
	mov (r5), r0
	bis #0100000, r0
	br complete
finished
	mov (r5), r0
complete
	add #000177, r0
	mov r0, (r5)
	mov (sp)+, r5
	mov (sp)+, r4
	mov (sp)+, r3
	mov (sp)+, r2
	mov (sp)+, r1
	mov (sp)+, r0
	br service_rti
	org 0160476
service_rti
	rti
