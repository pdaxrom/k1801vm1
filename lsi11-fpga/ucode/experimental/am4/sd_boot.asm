	cpu dcj-11

sd_data equ 0177500
sd_control equ 0177502
diag_value equ 0157774
diag_stage equ 0157776

; This ordinary PDP-11 bootstrap is packed into the seven unused physical bits
; of each AM4 MicROM word.  The AM4 reads it through the second DP8KC port.
	org 004000
start
	mov #004000, sp
	clr @#diag_value
	clr @#diag_stage
	clr r1

	; Match the proven Stable bootstrap's power-settle interval.  The AM4
	; profile has no guest-visible tick counter yet, so one full 16-bit SOB
	; sweep supplies a conservative delay before the first SD clocks.
	clr r3
power_settle
	sob r3, power_settle

	; At least 80 clocks with CS high before CMD0.
	mov #000012, r3
power_clocks
	mov #000377, @#sd_data
	sob r3, power_clocks

	inc r1			; diagnostic stage: CMD0
	clr r5			; slow clock, CS low
	mov #cmd0, r2
	jsr pc, command
	cmp #1, r0
	bne failed
	jsr pc, close_card

	inc r1			; diagnostic stage: CMD8
	mov #cmd8, r2
	jsr pc, command
	cmp #1, r0
	bne failed
	mov @#sd_data, r0
	bne failed
	mov @#sd_data, r0
	bne failed
	mov @#sd_data, r0
	cmp #1, r0
	bne failed
	mov @#sd_data, r0
	cmp #0252, r0
	bne failed
	jsr pc, close_card

	; ACMD41 may remain idle while the card finishes initialization.  At the
	; 195.6-kHz slow clock, 4096 command pairs provide a multi-second bound
	; comparable to the proven Stable bootstrap's two-second timer window.
	mov #010000, r4
initialize
	inc r1			; diagnostic stage: CMD55
	mov #cmd55, r2
	jsr pc, command
	bit #0177776, r0
	bne failed
	; Match the hardware-proven Stable transport: finish CMD55 with CS high
	; and trailing clocks, then start ACMD41 as a new SPI transaction.  APP_CMD
	; state survives CS deassertion on SD cards, while some cards stop answering
	; after many command packets kept under one continuous selection.
	jsr pc, close_card
	inc r1			; diagnostic stage: ACMD41
	mov #acmd41, r2
	jsr pc, command
	tst r0
	beq initialized
	cmp #1, r0
	bne failed
	jsr pc, close_card
	sob r4, initialize
	br failed

initialized
	jsr pc, close_card
	inc r1			; diagnostic stage: CMD58/OCR
	mov #cmd58, r2
	jsr pc, command
	tst r0
	bne failed
	mov @#sd_data, r0
	bic #0177477, r0		; require OCR power-up and CCS bits
	cmp #000300, r0
	bne failed
	mov @#sd_data, r0
	mov @#sd_data, r0
	mov @#sd_data, r0
	jsr pc, close_card

	mov #2, r5			; fast clock, CS low
	clr r4				; sector destination in FRAM
	inc r1			; diagnostic stage: CMD17 LBA0
	mov #cmd17_lba0, r2
	jsr pc, read_sector
	inc r1			; diagnostic stage: CMD17 LBA1
	mov #cmd17_lba1, r2
	jsr pc, read_sector

	; Stable two-sector entry ABI.  Bit 2 arms removal of the boot/ODT
	; overlay; CLR PC is still fetched from boot ROM before address zero
	; exposes ordinary FRAM and leaves PSW.Z set.
	mov #002000, sp
	clr r0
	mov #0177440, r1
	clr r2
	clr r3
	mov #002020, r4
	clr r5
	mov #7, @#sd_control
	clr pc

failed
	mov r0, @#diag_value
	mov r1, @#diag_stage
	halt
	br failed

; Send a six-byte command from R2 and return a valid idle/ready R1 byte in R0.
; Some physical cards can expose a transient low-MSB byte before their R1;
; this bootstrap only accepts the two states it can handle, 0 and 1.
command
	mov r5, @#sd_control
	mov #6, r3
command_bytes
	movb (r2)+, r0
	mov r0, @#sd_data
	sob r3, command_bytes
	mov #000020, r3
command_response
	mov @#sd_data, r0
	bit #0177776, r0
	beq command_done
	sob r3, command_response
	br failed
command_done
	rts pc

; Keep CS asserted briefly after the last response/data byte, then deassert it
; and provide one trailing byte without changing R5.  The physical HC1200 card
; needs this margin; J11_BOOT_TRACE supplied it accidentally through UART I/O.
close_card
	mov #0100000, r3
close_card_hold
	sob r3, close_card_hold
	mov r5, r0
	inc r0			; R5 is always 0 or 2, so this sets only CS-high bit 0
	mov r0, @#sd_control
	mov #000377, @#sd_data
	rts pc

; Read one 512-byte sector through the byte service into sequential FRAM.
read_sector
	jsr pc, command
	tst r0
	bne failed
	clr r3
wait_token
	mov @#sd_data, r0
	cmp #000376, r0
	beq sector_data
	cmp #000377, r0
	bne failed
	sob r3, wait_token
	br failed
sector_data
	mov #001000, r3
sector_loop
	mov @#sd_data, r0
	movb r0, (r4)+
	sob r3, sector_loop
	mov @#sd_data, r0		; discard CRC16
	mov @#sd_data, r0
	jsr pc, close_card
	rts pc

cmd0
	db 0100, 0, 0, 0, 0, 0225
cmd8
	db 0110, 0, 0, 1, 0252, 0207
cmd55
	db 0167, 0, 0, 0, 0, 1
acmd41
	db 0151, 0100, 0, 0, 0, 1
cmd58
	db 0172, 0, 0, 0, 0, 1
cmd17_lba0
	db 0121, 0, 0, 0, 0, 1
cmd17_lba1
	db 0121, 0, 0, 0, 1, 1
