        org     2000

        ; Basic FIS Instructions Test
        ; Note: FIS instruction operand R must point to a float32 pseudo-F11 structure (2 words)
        ; Trap vector 0244 is used for FIS division by zero or other math errors
        ; Trap vector 0010 is used for illegal instructions (e.g., when trying to use Rn when SEL0 disables FIS)

start:
        mov     #01000, sp    ; Setup stack pointer
        ; --- Setup vector for FIS errors ---
        mov     #fis_err_handler, @#0244
        mov     #000200, @#0246

        ; --- Setup normal float stack and registers ---
        mov     #float_data_a, r0
        mov     #float_data_b, r1

        ; --- FDIV division by zero test ---
        ; Put 0.0 into float_data_a
        mov     #0, float_data_a
        mov     #0, float_data_a+2
        ; Put 1.0 into float_data_b (or any number)
        mov     #40000, float_data_b  ; Sign 0, Exponent=129 (129-128 = 1, IEEE 127) -> 200 << 7
        mov     #0, float_data_b+2
        
        ; FDIV R0
        ; Operands for FDIV: A (r0) is Divisor, B (r0+4) is Dividend. We need them adjacent.
        ; actually for FIS, r0 must point to top of stack.
        ; Let's build a small float stack on general stack.
        mov     #040000, -(sp)    ; B high: 1.0
        mov     #000000, -(sp)    ; B low
        mov     #000000, -(sp)    ; A high: 0.0
        mov     #000000, -(sp)    ; A low
        mov     sp, r0

        ; clear trap flag indicator
        mov     #0, trap_flag

        ; FDIV R0 -> A is R0, B is R0+4
        ; 075030 is FDIV R0
        dw      075030

        ; We should have trapped by now!
        cmp     trap_flag, #1
        bne     fail

success:
        mov     #1, r0        ; Indicate success
        halt                  ; Normal exit (Success)
        br      success       ; Spin

fail:
        mov     #0xFFFF, r0   ; Indicate failure
        halt
        br      fail          ; Spin

fis_err_handler:
        mov     #1, trap_flag ; Mark that we handled the trap
        rti

trap_flag:
        dw      0

        even
float_data_a:   dw 0, 0
float_data_b:   dw 0, 0
