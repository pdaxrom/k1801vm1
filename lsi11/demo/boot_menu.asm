        org     0100000

DL11_RCSR equ 0177560
DL11_RBUF equ 0177562
DL11_TCSR equ 0177564
DL11_TBUF equ 0177566

RKDS      equ 0177400
RHCS1     equ 0177440
XPCS1     equ 0176700
RQ_SA     equ 0172152
RLCS      equ 0174400
TQ_SA     equ 0174502

KIND_RK   equ 1
KIND_RH   equ 2
KIND_XP   equ 3
KIND_RQ   equ 4
KIND_RL   equ 5
KIND_TQ   equ 6

ASC_0     equ 0060
ASC_1     equ 0061
ASC_2     equ 0062
ASC_3     equ 0063
ASC_4     equ 0064
ASC_5     equ 0065
ASC_6     equ 0066
ASC_7     equ 0067
ASC_CR    equ 0015
ASC_LF    equ 0012

RQ_BOOT_ADDR   equ 016000
RQ_RPKT        equ 0007004
RQ_CPKT        equ 0007104
RQ_COMM        equ 0007204

TQ_BOOT_ADDR   equ 016000
TQ_B_CMDINT    equ 015000
TQ_B_RSPINT    equ 015002
TQ_B_RING      equ 015004
TQ_B_RSPH      equ 015014
TQ_B_TKRSP     equ 015020
TQ_B_CMDH      equ 015100
TQ_B_TKCMD     equ 015104
TQ_B_UNIT      equ 015110

start:
        mtps    #000000
        mov     #stack_top, sp

        mov     @#000004, saved_vec4_pc
        mov     @#000006, saved_vec4_psw
        mov     #nxm_handler, @#000004
        mov     #000340, @#000006
        clr     probe_resume

        mov     #msg_banner, r1
        jsr     pc, print_str

        jsr     pc, detect_controllers
        mov     saved_vec4_pc, @#000004
        mov     saved_vec4_psw, @#000006
        tstb    present_any
        bne     has_any

        mov     #msg_none, r1
        jsr     pc, print_str
        halt
halt_none:
        br      halt_none

has_any:
        mov     #msg_found, r1
        jsr     pc, print_str

        tstb    present_rk
        beq     list_rh
        mov     #msg_line_rk, r1
        jsr     pc, print_str
list_rh:
        tstb    present_rh
        beq     list_xp
        mov     #msg_line_rh, r1
        jsr     pc, print_str
list_xp:
        tstb    present_xp
        beq     list_rq
        mov     #msg_line_xp, r1
        jsr     pc, print_str
list_rq:
        tstb    present_rq
        beq     list_rl
        mov     #msg_line_rq, r1
        jsr     pc, print_str
list_rl:
        tstb    present_rl
        beq     list_tq
        mov     #msg_line_rl, r1
        jsr     pc, print_str
list_tq:
        tstb    present_tq
        beq     select_controller
        mov     #msg_line_tq, r1
        jsr     pc, print_str

select_controller:
        mov     #msg_choose_ctl, r1
        jsr     pc, print_str
ctl_loop:
        jsr     pc, read_key
        jsr     pc, echo_char_crlf

        cmp     r0, #ASC_1
        beq     ctl_rk
        cmp     r0, #ASC_2
        beq     ctl_rh
        cmp     r0, #ASC_3
        beq     ctl_xp
        cmp     r0, #ASC_4
        beq     ctl_rq
        cmp     r0, #ASC_5
        beq     ctl_rl
        cmp     r0, #ASC_6
        beq     ctl_tq

        mov     #msg_bad_ctl, r1
        jsr     pc, print_str
        br      ctl_loop

ctl_rk:
        tstb    present_rk
        bne     ctl_rk_ok
        mov     #msg_not_present, r1
        jsr     pc, print_str
        br      ctl_loop
ctl_rk_ok:
        movb    #KIND_RK, selected_kind
        movb    #7, max_unit
        br      select_unit

ctl_rh:
        tstb    present_rh
        bne     ctl_rh_ok
        mov     #msg_not_present, r1
        jsr     pc, print_str
        br      ctl_loop
ctl_rh_ok:
        movb    #KIND_RH, selected_kind
        movb    #7, max_unit
        br      select_unit

ctl_xp:
        tstb    present_xp
        bne     ctl_xp_ok
        mov     #msg_not_present, r1
        jsr     pc, print_str
        br      ctl_loop
ctl_xp_ok:
        movb    #KIND_XP, selected_kind
        movb    #7, max_unit
        br      select_unit

ctl_rq:
        tstb    present_rq
        bne     ctl_rq_ok
        mov     #msg_not_present, r1
        jsr     pc, print_str
        br      ctl_loop
ctl_rq_ok:
        movb    #KIND_RQ, selected_kind
        movb    #3, max_unit
        br      select_unit

ctl_rl:
        tstb    present_rl
        bne     ctl_rl_ok
        mov     #msg_not_present, r1
        jsr     pc, print_str
        br      ctl_loop
ctl_rl_ok:
        movb    #KIND_RL, selected_kind
        movb    #3, max_unit
        br      select_unit

ctl_tq:
        tstb    present_tq
        bne     ctl_tq_ok
        mov     #msg_not_present, r1
        jsr     pc, print_str
        br      ctl_loop
ctl_tq_ok:
        movb    #KIND_TQ, selected_kind
        movb    #7, max_unit

select_unit:
        cmpb    max_unit, #3
        beq     unit_prompt_3
        mov     #msg_choose_unit_7, r1
        jsr     pc, print_str
        br      unit_loop
unit_prompt_3:
        mov     #msg_choose_unit_3, r1
        jsr     pc, print_str

unit_loop:
        jsr     pc, read_key
        jsr     pc, echo_char_crlf

        cmp     r0, #ASC_0
        blt     bad_unit
        cmp     r0, #ASC_7
        bgt     bad_unit

        sub     #ASC_0, r0
        movb    max_unit, r1
        cmp     r0, r1
        bgt     bad_unit

        mov     r0, selected_unit
        br      boot_dispatch

bad_unit:
        mov     #msg_bad_unit, r1
        jsr     pc, print_str
        br      unit_loop

boot_dispatch:
        cmpb    selected_kind, #KIND_RK
        beq     boot_rk
        cmpb    selected_kind, #KIND_RH
        beq     boot_rh
        cmpb    selected_kind, #KIND_XP
        beq     boot_xp
        cmpb    selected_kind, #KIND_RQ
        beq     boot_rq
        cmpb    selected_kind, #KIND_RL
        beq     boot_rl
        cmpb    selected_kind, #KIND_TQ
        beq     boot_tq

        mov     #msg_internal, r1
        jsr     pc, print_str
        halt
halt_internal:
        br      halt_internal

boot_rk:
        mov     #msg_boot_rk, r1
        jsr     pc, print_str
        jmp     rk_boot_run

boot_rh:
        mov     #msg_boot_rh, r1
        jsr     pc, print_str
        jmp     rh_boot_run

boot_xp:
        mov     #msg_boot_xp, r1
        jsr     pc, print_str
        jmp     xp_boot_run

boot_rq:
        mov     #msg_boot_rq, r1
        jsr     pc, print_str
        jmp     rq_boot_run

boot_rl:
        mov     #msg_boot_rl, r1
        jsr     pc, print_str
        jmp     rl_boot_run

boot_tq:
        mov     #msg_boot_tq, r1
        jsr     pc, print_str
        jmp     tq_boot_run

detect_controllers:
        clr     present_any

        mov     #RKDS, r0
        jsr     pc, probe_word
        movb    r0, present_rk
        tstb    present_rk
        beq     det_rh
        bisb    #1, present_any

det_rh:
        mov     #RHCS1, r0
        jsr     pc, probe_word
        movb    r0, present_rh
        tstb    present_rh
        beq     det_xp
        bisb    #1, present_any

det_xp:
        mov     #XPCS1, r0
        jsr     pc, probe_word
        movb    r0, present_xp
        tstb    present_xp
        beq     det_rq
        bisb    #1, present_any

det_rq:
        mov     #RQ_SA, r0
        jsr     pc, probe_word
        movb    r0, present_rq
        tstb    present_rq
        beq     det_rl
        bisb    #1, present_any

det_rl:
        mov     #RLCS, r0
        jsr     pc, probe_word
        movb    r0, present_rl
        tstb    present_rl
        beq     det_tq
        bisb    #1, present_any

det_tq:
        mov     #TQ_SA, r0
        jsr     pc, probe_word
        movb    r0, present_tq
        tstb    present_tq
        beq     det_done
        bisb    #1, present_any

det_done:
        rts     pc

probe_word:
        mov     r1, -(sp)
        clrb    probe_nxm
        mov     #probe_resume_pc, probe_resume
        mov     (r0), r1
probe_resume_pc:
        clr     probe_resume
        tstb    probe_nxm
        bne     probe_fail
        mov     #1, r0
        br      probe_done
probe_fail:
        clr     r0
probe_done:
        mov     (sp)+, r1
        rts     pc

nxm_handler:
        mov     r0, nxm_saved_r0
        movb    #1, probe_nxm
        mov     probe_resume, r0
        beq     nxm_done
        mov     r0, (sp)
nxm_done:
        mov     nxm_saved_r0, r0
        rti

putc:
putc_wait:
        movb    @#DL11_TCSR, r2
        bitb    #0200, r2
        beq     putc_wait
        movb    r0, @#DL11_TBUF
        rts     pc

getc:
getc_wait:
        movb    @#DL11_RCSR, r0
        bitb    #0200, r0
        beq     getc_wait
        movb    @#DL11_RBUF, r0
        bic     #177600, r0
        rts     pc

read_key:
        jsr     pc, getc
        cmp     r0, #ASC_CR
        beq     read_key
        cmp     r0, #ASC_LF
        beq     read_key
        rts     pc

echo_char_crlf:
        mov     r0, -(sp)
        jsr     pc, putc
        mov     #ASC_CR, r0
        jsr     pc, putc
        mov     #ASC_LF, r0
        jsr     pc, putc
        mov     (sp)+, r0
        rts     pc

print_str:
        movb    (r1)+, r0
        beq     print_done
        jsr     pc, putc
        br      print_str
print_done:
        rts     pc

msg_banner:
        DB      "BOOT MENU @0100000\r\n", 0
msg_found:
        DB      "Detected controllers:\r\n", 0
msg_none:
        DB      "No RK11/RH11/XP/RP/RQ/RL11/TQ11 controllers detected.\r\n", 0
msg_line_rk:
        DB      "  1 - RK11 (rk0..rk7)\r\n", 0
msg_line_rh:
        DB      "  2 - RH11/HK (rh0..rh7)\r\n", 0
msg_line_xp:
        DB      "  3 - XP/RP (xp0..xp7)\r\n", 0
msg_line_rq:
        DB      "  4 - RQ (MSCP) (rq0..rq3)\r\n", 0
msg_line_rl:
        DB      "  5 - RL11 (rl0..rl3)\r\n", 0
msg_line_tq:
        DB      "  6 - TQ11/TMSCP (tq0..tq7)\r\n", 0
msg_choose_ctl:
        DB      "Select controller [1/2/3/4/5/6]: ", 0
msg_bad_ctl:
        DB      "Invalid controller selection.\r\n", 0
msg_not_present:
        DB      "Controller not present in this system.\r\n", 0
msg_choose_unit_7:
        DB      "Select unit [0..7]: ", 0
msg_choose_unit_3:
        DB      "Select unit [0..3]: ", 0
msg_bad_unit:
        DB      "Invalid unit number.\r\n", 0
msg_boot_rk:
        DB      "Booting RK bootstrap...\r\n", 0
msg_boot_rh:
        DB      "Booting RH/HK bootstrap...\r\n", 0
msg_boot_xp:
        DB      "Booting XP/RP bootstrap...\r\n", 0
msg_boot_rq:
        DB      "Booting RQ bootstrap...\r\n", 0
msg_boot_rl:
        DB      "Booting RL bootstrap...\r\n", 0
msg_boot_tq:
        DB      "Booting TQ bootstrap...\r\n", 0
msg_internal:
        DB      "Internal dispatch error.\r\n", 0

        even
present_any:
        DB      0
present_rk:
        DB      0
present_rh:
        DB      0
present_xp:
        DB      0
present_rq:
        DB      0
present_rl:
        DB      0
present_tq:
        DB      0
selected_kind:
        DB      0
max_unit:
        DB      0
        even
selected_unit:
        DW      0
probe_resume:
        DW      0
nxm_saved_r0:
        DW      0
saved_vec4_pc:
        DW      0
saved_vec4_psw:
        DW      0
probe_nxm:
        DB      0
        even

rk_boot_run:
        mov     #002000, sp
        mov     selected_unit, r0
        mov     r0, r3
        swab    r3
        asl     r3
        asl     r3
        asl     r3
        asl     r3
        asl     r3
        mov     #0177412, r1
        mov     r3, (r1)
        clr     -(r1)
        mov     #0177000, -(r1)
        mov     #0000005, -(r1)
        clr     r2
        clr     r3
        mov     #002020, r4
        clr     r5
rk_wait:
        tstb    (r1)
        bpl     rk_wait
        clrb    (r1)
        clr     pc

rh_boot_run:
        mov     #002000, sp
        mov     selected_unit, r0
        mov     #0177440, r1
        mov     #0000040, 10(r1)
        mov     r0, 10(r1)
rh_wait_ds:
        mov     12(r1), r2
        bpl     rh_wait_ds
        bic     #0177377, r2
        asl     r2
        asl     r2
        mov     #0000003, r3
        bis     r2, r3
        mov     r3, (r1)
rh_wait1:
        tstb    (r1)
        bpl     rh_wait1
        mov     #0177000, 2(r1)
        clr     4(r1)
        clr     6(r1)
        clr     20(r1)
        mov     #0000021, r3
        bis     r2, r3
        mov     r3, (r1)
rh_wait2:
        tstb    (r1)
        bpl     rh_wait2
        clr     r2
        clr     r3
        mov     #002020, r4
        clr     r5
        clr     pc

xp_boot_run:
        mov     #002000, sp
        mov     selected_unit, r0
        mov     #0176700, r1
        mov     #0000040, 10(r1)
        mov     r0, 10(r1)
        mov     #0000021, (r1)
        mov     #0010000, 32(r1)
        mov     #0177000, 2(r1)
        clr     4(r1)
        clr     6(r1)
        clr     34(r1)
        mov     #0000071, (r1)
xp_wait:
        tstb    (r1)
        bpl     xp_wait
        clr     r2
        clr     r3
        mov     #002020, r4
        clr     r5
        clrb    (r1)
        clr     pc

rq_boot_run:
        mov     #RQ_BOOT_ADDR, sp
        mov     selected_unit, r0
        mov     #0172150, r1
        mov     r0, @#(RQ_BOOT_ADDR+010)
        mov     r1, @#(RQ_BOOT_ADDR+014)
        mov     #rq_it, r4
        mov     #0004000, r5
        mov     r1, r2
        clr     (r2)+
rq_step1:
        tst     (r2)
        bpl     rq_step1_ok
        halt
rq_step1_ok:
        bit     r5, (r2)
        beq     rq_step1
        mov     (r4)+, (r2)
        asl     r5
        bpl     rq_step1

rq_step2:
        tstb    (r4)
        beq     rq_done
        mov     #RQ_RPKT-4, r2
rq_clear:
        clr     (r2)+
        cmp     r2, #RQ_COMM
        blt     rq_clear
        movb    (r4)+, @#(RQ_CPKT-4)
        movb    r0, @#(RQ_CPKT+4)
        movb    (r4)+, @#(RQ_CPKT+10)
        movb    (r4)+, @#(RQ_CPKT+15)
        mov     #RQ_RPKT, (r2)+
        mov     r5, (r2)+
        mov     #RQ_CPKT, (r2)+
        mov     r5, (r2)
        cmp     -(r2), -(r2)
        tst     (r1)
rq_wait:
        tst     (r2)
        bmi     rq_wait
        tst     @#(RQ_RPKT+12)
        beq     rq_step2
        halt
rq_done:
        clr     (r1)
        clr     r3
        mov     #016020, r4
        clr     r5
        clr     pc

rl_boot_run:
        mov     #0174400, r1
        mov     selected_unit, r0
        swab    r0
        mov     r0, r2
        mov     #0000013, r3
        bis     r2, r3
        mov     r3, 4(r1)
        mov     #0000004, r3
        bis     r2, r3
        mov     r3, (r1)
rl_wait1:
        tstb    (r1)
        bpl     rl_wait1
        clr     2(r1)
        clr     4(r1)
        mov     #0177400, 6(r1)
        mov     #0000014, r3
        bis     r2, r3
        mov     r3, (r1)
rl_wait2:
        tstb    (r1)
        bpl     rl_wait2
        clr     pc

tq_boot_run:
        mov     #016000, sp
        mov     selected_unit, r0
        mov     #0174500, r1
        clr     (r1)+
        clr     r2
tq_clear:
        clr     (r2)+
        cmp     r2, #015776
        blt     tq_clear

tq_step1:
        tst     (r1)
        bpl     tq_step1_ok
        halt
tq_step1_ok:
        bit     #0004000, (r1)
        beq     tq_step1
        mov     #0100000, (r1)

tq_step2:
        tst     (r1)
        bpl     tq_step2_ok
        halt
tq_step2_ok:
        bit     #0010000, (r1)
        beq     tq_step2
        mov     #TQ_B_RING, (r1)

tq_step3:
        tst     (r1)
        bpl     tq_step3_ok
        halt
tq_step3_ok:
        bit     #0020000, (r1)
        beq     tq_step3
        clr     (r1)

tq_step4:
        tst     (r1)
        bpl     tq_step4_ok
        halt
tq_step4_ok:
        bit     #0040000, (r1)
        beq     tq_step4
        mov     #0000001, (r1)

        mov     #0100000, r4

        mov     #0000400, @#(TQ_B_CMDH + 2)
        mov     #0000044, @#TQ_B_CMDH
        mov     r0, @#TQ_B_UNIT
        mov     #0000011, @#(TQ_B_TKCMD + 10)
        mov     #0020000, @#(TQ_B_TKCMD + 12)
        mov     #TQ_B_RING, r2
        mov     #TQ_B_TKRSP, (r2)+
        mov     r2, r3
        mov     r4, (r3)+
        mov     #TQ_B_TKCMD, (r3)+
        mov     r4, (r3)+
        tst     -(r1)
tq_wait_onl:
        tst     (r2)
        bmi     tq_wait_onl
        tstb    @#(TQ_B_TKRSP + 12)
        beq     tq_onl_ok
        halt
tq_onl_ok:
        mov     #(TQ_B_TKCMD + 10), r3
        mov     #0000045, (r3)+
        mov     #0020002, (r3)+
        mov     #0000001, (r3)+
        clr     (r3)+
        clr     (r3)+
        clr     (r3)+
        mov     r4, (r2)
        mov     r4, @#(TQ_B_RING + 6)
        tst     (r1)
tq_wait_rew:
        tst     (r2)
        bmi     tq_wait_rew
        tstb    @#(TQ_B_TKRSP + 12)
        beq     tq_rew_ok
        halt
tq_rew_ok:
        mov     #(TQ_B_TKCMD + 10), r3
        mov     #0000041, (r3)+
        mov     #0020000, (r3)+
        mov     #0001000, (r3)+
        clr     (r3)+
        clr     (r3)+
        mov     r4, (r2)
        mov     r4, @#(TQ_B_RING + 6)
        tst     (r1)
tq_wait_read:
        tst     (r2)
        bmi     tq_wait_read
        tstb    @#(TQ_B_TKRSP + 12)
        beq     tq_read_ok
        halt
tq_read_ok:
        clr     r3
        mov     #016020, r4
        clr     r5
        clr     pc

        even
rq_it:
        DW      0100000
        DW      RQ_COMM
        DW      0000000
        DW      0000001
        DB      020, 011
        DB      000, 040
        DB      041, 002
        DW      0000000

stack_area:
        DS      128
stack_top:
