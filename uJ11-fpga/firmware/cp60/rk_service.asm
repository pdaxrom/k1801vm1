; CP60: RK611 recovery required by RT-11 DM.SYS after an SD error.
; Based on firmware/rk_service.asm; source and fixed RTI ABI retained.
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
; Data commands use bit 4; only RECALIBRATE joins this private service.
; The virtual drive has no mechanical position, so it completes immediately.
	mov (r5), r0
	bit #000020, r0
	bne data_service
	jmp recal_complete
data_service
	mov 002(r5), r3

	; DB is private while the service owns the CPU; use it for current LBA.
	mov 006(r5), r0
	mov r0, r2
	; DA head bits become the low three bits after SWAB; this is equivalent to
	; ASH #-8 here and saves one private-service word.
	swab r0
	bic #0177770, r0
	mul #000026, r0
	bic #0177740, r2
	add r2, r1
	mov r1, r2
	mov 020(r5), r0
	mul #000102, r0
	add r2, r1
	mov r1, 022(r5)
	mov 004(r5), r2

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
	; CRC is ignored after card initialization, but its end bit must be one.
	; The active RK command in CS1 already has GO set, so reuse its low byte.
	mov (r5), (r4)
	mov r4, r0
response
	tst (r4)
	beq response_done
	sob r0, response
	br failed
response_done
	bit #2, (r5)
	bne write_sector
wait_token
	; Reuse the still-large response timeout as the data-token timeout.
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
	beq discard_entry
	sob r0, copy_word
	br sector_done
discard_word
	tst (r4)
	tst (r4)
discard_entry
	sob r0, discard_word
sector_done
	tst (r4)
	tst (r4)
	inc 002(r4)
	tst (r4)			; a read clocks the same idle FF byte
	mov r3, 002(r5)
	mov r2, 004(r5)
	inc 022(r5)

	; RK06 geometry is 22 sectors x 3 heads.  Advance DA after every
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

	; SD writes always transfer one complete 512-byte sector.  The RK611 write
	; sequence (DEC EK-RK067-UG-001, Figure 7-28) specifies that a partial final
	; WRITE stops memory DMA at WC zero and zero-fills the rest of the sector;
	; BA advances only for words actually transferred from memory.
	; After the accepted-token check, keep CS asserted but stop clocking while
	; the card programs the sector.  Tight back-to-back AM4 polls can leave the
	; physical card busy indefinitely.  The accepted response becomes six
	; below; reuse it as the number of widely spaced, bounded status polls.
write_sector
	mov #000376, (r4)
	; The patched AM4 MOVB path resolves a store-only destination EA, so this
	; memory-to-memory byte transfer does not read and clock SD_DATA first.
	mov #000400, r0
write_word
	movb (r2)+, (r4)
	movb (r2)+, (r4)
	inc r3
	beq write_zero_entry
	sob r0, write_word
	br write_data_done
write_zero
	clr (r4)
	clr (r4)
write_zero_entry
	sob r0, write_zero
write_data_done
	dec r0				; SOB left zero; use FFFF for both CRC bytes
	mov r0, (r4)
	mov r0, (r4)
write_response
	mov (r4), r1
	incb r1			; FF becomes zero; status 5/13/15 becomes 6/14/16
	bne write_response_done
	; R0 still holds FFFF from the CRC bytes. Bound absent-card/FF polling;
	; individual SPI reads acknowledge, so the CPU bus timer cannot do this.
	sob r0, write_response
	br failed
write_response_done
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

	; Two literal command bytes; fixed RTI remains at 160476 below.
command_bytes
	dw 000121
	dw 000130
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

	org 0160500
failed
; Always leave CS high and fast mode selected, including timeout failures.
; Clock one idle byte before exposing RK completion to the guest.
	mov #3, 002(r4)
	tst (r4)
	mov #020000, 014(r5)
	mov (r5), r0
	bis #0100000, r0
	br complete

recal_complete
; EK-RK067-UG-001 sections 7.2.1 / 7.3: positioning completion sets DI.
; DONE alone is the controller response; DM.SYS waits for drive attention.
	bis #040000, r0
	br complete
