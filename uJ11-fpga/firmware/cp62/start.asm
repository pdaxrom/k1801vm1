; Fits the 54 bytes following the unchanged 426-byte SD bootstrap.
; The build verifies every fixed address against both assembled listings.
; Cold reset already selected HALT; these are all ordinary logical MOVs.
	cpu dcj-11
	org 010652
start
	mov #010000, r0         ; Copy original SD bootstrap to HALT RAM first
	mov #004000, r1
	mov #000325, r2
install_boot
	mov (r0)+, r3
	mov r3, (r1)
	cmp r3, (r1)+
	bne failed
	sob r2, install_boot
	mov #011534, r0         ; ROM payload after CP60's 348-byte RK firmware
	mov #000200, r1
	mov #000122, r2         ; Checked by build against resident.bin
install
	mov (r0)+, r3
	mov r3, (r1)
	cmp r3, (r1)+
	bne failed
	sob r2, install
	jmp @#000322            ; resident initialize label, checked by build
failed
	br failed               ; Remain in ROM/HALT if FRAM readback failed.
start_end
