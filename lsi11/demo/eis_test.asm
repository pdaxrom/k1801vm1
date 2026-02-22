        org     2000

        ; Basic EIS Instructions Test

start:
        mov     #01000, sp    ; Setup stack pointer

        ; --- Setup vector for Illegal Instructions ---
        mov     #ill_handler, @#010
        mov     #000200, @#012
        ; --- MUL Test ---
        mov     #10, r0
        mov     #10, r1
        mul     r1, r0
        ; R0 should be high 16 bits (0)
        ; R1 should be low 16 bits (100 = 0144)
        cmp     r0, #0
        bne     fail
        cmp     r1, #0144
        bne     fail

        ; --- DIV Test ---
        mov     #0, r0        ; high dividend
        mov     #0144, r1     ; low dividend (100)
        mov     #10, r2       ; divisor
        div     r2, r0        
        ; R0 should be quotient (10 = 012)
        ; R1 should be remainder (0)
        cmp     r0, #012
        bne     fail
        cmp     r1, #0
        bne     fail

        ; --- ASH Test ---
        mov     #1, r0
        mov     #3, r1        ; Shift left by 3 (x8)
        ash     r1, r0
        cmp     r0, #10       ; 1 * 8 = 8 = 010
        bne     fail

        mov     #010, r0
        mov     #-3, r1       ; Shift right by 3 (/8)
        ash     r1, r0
        cmp     r0, #1
        bne     fail

        ; --- ASHC Test ---
        mov     #0, r0        ; High
        mov     #1, r1        ; Low
        mov     #3, r2        ; Shift left by 3
        ashc    r2, r0
        cmp     r0, #0
        bne     fail
        cmp     r1, #10       ; 010
        bne     fail

        mov     #0, trap_flag
        cmp     trap_flag, #1
        beq     fail

success:
        mov     #1, r0        ; Indicate success
        halt                  ; Normal exit (Success)
        br      success       ; Spin

fail:
        mov     #0xFFFF, r0   ; Indicate failure
        halt
        br      fail          ; Spin

ill_handler:
        mov     #1, trap_flag
        rti

trap_flag:
        dw      0
