        org     0100000

DL11_RCSR equ 0177560
DL11_RBUF equ 0177562
DL11_TCSR equ 0177564
DL11_TBUF equ 0177566

RKDS      equ 0177400
RHCS1     equ 0177440
RLCS      equ 0174400
TQ_SA     equ 0174502

KIND_RK   equ 1
KIND_RH   equ 2
KIND_RL   equ 3
KIND_TQ   equ 4

ASC_0     equ 0060
ASC_1     equ 0061
ASC_2     equ 0062
ASC_3     equ 0063
ASC_4     equ 0064
ASC_7     equ 0067
ASC_CR    equ 0015
ASC_LF    equ 0012

RK_BOOT_ADDR      equ 002000
RK_BOOT_ENTRY     equ 002002
RK_BOOT_UNIT_SLOT equ 002010

RL_BOOT_ADDR        equ 002000
RL_BOOT_ENTRY       equ 002000
RL_BOOT_GSTAT_SLOT  equ 002014
RL_BOOT_READ_SLOT   equ 002036

RH_BOOT_ADDR      equ 002000
RH_BOOT_ENTRY     equ 002002
RH_BOOT_UNIT_SLOT equ 002010

TQ_BOOT_ADDR   equ 016000
TQ_BOOT_ENTRY  equ 016002
TQ_BOOT_UNIT   equ 016010
TQ_BOOT_CSR    equ 016014
TQ_B_CMDINT    equ 015000
TQ_B_RSPINT    equ 015002
TQ_B_RING      equ 015004
TQ_B_RSPH      equ 015014
TQ_B_TKRSP     equ 015020
TQ_B_CMDH      equ 015100
TQ_B_TKCMD     equ 015104
TQ_B_UNIT      equ 015110

RK_BOOT_WORDS  equ 29
RL_BOOT_WORDS  equ 21
RH_BOOT_WORDS  equ 46
TQ_BOOT_WORDS  equ 105

start:
        mtps    #000000
        mov     #stack_top, sp

        mov     #nxm_handler, @#000004
        mov     #000340, @#000006
        clr     probe_resume

        mov     #msg_banner, r1
        jsr     pc, print_str

        jsr     pc, detect_controllers
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
        beq     list_rl
        mov     #msg_line_rh, r1
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
        beq     ctl_rl
        cmp     r0, #ASC_4
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
        mov     #rk_bootstrap, r0
        mov     #RK_BOOT_ADDR, r1
        mov     #RK_BOOT_WORDS, r2
        jsr     pc, copy_words
        mov     selected_unit, r0
        mov     r0, @#RK_BOOT_UNIT_SLOT
        mov     #RK_BOOT_ENTRY, pc

boot_rh:
        mov     #msg_boot_rh, r1
        jsr     pc, print_str
        mov     #rh_bootstrap, r0
        mov     #RH_BOOT_ADDR, r1
        mov     #RH_BOOT_WORDS, r2
        jsr     pc, copy_words
        mov     selected_unit, r0
        mov     r0, @#RH_BOOT_UNIT_SLOT
        mov     #RH_BOOT_ENTRY, pc

boot_rl:
        mov     #msg_boot_rl, r1
        jsr     pc, print_str
        mov     #rl_bootstrap, r0
        mov     #RL_BOOT_ADDR, r1
        mov     #RL_BOOT_WORDS, r2
        jsr     pc, copy_words

        mov     selected_unit, r0
        swab    r0
        mov     r0, r1
        bis     #0000004, r1
        mov     r1, @#RL_BOOT_GSTAT_SLOT
        mov     r0, r1
        bis     #0000014, r1
        mov     r1, @#RL_BOOT_READ_SLOT
        mov     #RL_BOOT_ENTRY, pc

boot_tq:
        mov     #msg_boot_tq, r1
        jsr     pc, print_str
        mov     #tq_bootstrap, r0
        mov     #TQ_BOOT_ADDR, r1
        mov     #TQ_BOOT_WORDS, r2
        jsr     pc, copy_words
        mov     selected_unit, r0
        mov     r0, @#TQ_BOOT_UNIT
        mov     #0174500, r0
        mov     r0, @#TQ_BOOT_CSR
        mov     #TQ_BOOT_ENTRY, pc

copy_words:
        tst     r2
        beq     copy_done
copy_loop:
        mov     (r0)+, (r1)+
        sob     r2, copy_loop
copy_done:
        rts     pc

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
        DB      "No RK11/RH11/RL11/TQ11 controllers detected.\r\n", 0
msg_line_rk:
        DB      "  1 - RK11 (rk0..rk7)\r\n", 0
msg_line_rh:
        DB      "  2 - RH11/HK (rh0..rh7)\r\n", 0
msg_line_rl:
        DB      "  3 - RL11 (rl0..rl3)\r\n", 0
msg_line_tq:
        DB      "  4 - TQ11/TMSCP (tq0..tq7)\r\n", 0
msg_choose_ctl:
        DB      "Select controller [R/H/L/T]: ", 0
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
probe_nxm:
        DB      0
        even

rk_bootstrap:
        DW      0042113
        DW      0012706
        DW      RK_BOOT_ADDR
        DW      0012700
        DW      0000000
        DW      0010003
        DW      0000303
        DW      0006303
        DW      0006303
        DW      0006303
        DW      0006303
        DW      0006303
        DW      0012701
        DW      0177412
        DW      0010311
        DW      0005041
        DW      0012741
        DW      0177000
        DW      0012741
        DW      0000005
        DW      0005002
        DW      0005003
        DW      0012704
        DW      RK_BOOT_ADDR + 000020
        DW      0005005
        DW      0105711
        DW      0100376
        DW      0105011
        DW      0005007

rl_bootstrap:
        DW      0012701
        DW      0174400
        DW      0012761
        DW      0000013
        DW      0000004
        DW      0012711
        DW      0000004
        DW      0105711
        DW      0100376
        DW      0005061
        DW      0000002
        DW      0005061
        DW      0000004
        DW      0012761
        DW      0177400
        DW      0000006
        DW      0012711
        DW      0000014
        DW      0105711
        DW      0100376
        DW      0005007

rh_bootstrap:
        DW      0042115
        DW      0012706
        DW      RH_BOOT_ADDR
        DW      0012700
        DW      0000000
        DW      0012701
        DW      0177440
        DW      0012761
        DW      0000040
        DW      0000010
        DW      0010061
        DW      0000010
        DW      0016102
        DW      0000012
        DW      0100375
        DW      0042702
        DW      0177377
        DW      0006302
        DW      0006302
        DW      0012703
        DW      0000003
        DW      0050203
        DW      0010311
        DW      0105711
        DW      0100376
        DW      0012761
        DW      0177000
        DW      0000002
        DW      0005061
        DW      0000004
        DW      0005061
        DW      0000006
        DW      0005061
        DW      0000020
        DW      0012703
        DW      0000021
        DW      0050203
        DW      0010311
        DW      0105711
        DW      0100376
        DW      0005002
        DW      0005003
        DW      0012704
        DW      RH_BOOT_ADDR + 000020
        DW      0005005
        DW      0005007

tq_bootstrap:
        DW      0046525
        DW      0012706
        DW      TQ_BOOT_ADDR
        DW      0012700
        DW      0000000
        DW      0012701
        DW      0174500
        DW      0005021
        DW      0012704
        DW      0004000
        DW      0005002
        DW      0005022
        DW      0020237
        DW      TQ_BOOT_ADDR - 000002
        DW      0103774
        DW      0012705
        DW      TQ_BOOT_ADDR + 000312
        DW      0005711
        DW      0100001
        DW      0000000
        DW      0030411
        DW      0001773
        DW      0012511
        DW      0006304
        DW      0100370
        DW      0012737
        DW      0000400
        DW      TQ_B_CMDH + 000002
        DW      0012737
        DW      0000044
        DW      TQ_B_CMDH
        DW      0010037
        DW      TQ_B_UNIT
        DW      0012737
        DW      0000011
        DW      TQ_B_TKCMD + 000010
        DW      0012737
        DW      0020000
        DW      TQ_B_TKCMD + 000012
        DW      0012702
        DW      TQ_B_RING
        DW      0012722
        DW      TQ_B_TKRSP
        DW      0010203
        DW      0010423
        DW      0012723
        DW      TQ_B_TKCMD
        DW      0010423
        DW      0005741
        DW      0005712
        DW      0100776
        DW      0105737
        DW      TQ_B_TKRSP + 000012
        DW      0001401
        DW      0000000
        DW      0012703
        DW      TQ_B_TKCMD + 000010
        DW      0012723
        DW      0000045
        DW      0012723
        DW      0020002
        DW      0012723
        DW      0000001
        DW      0005023
        DW      0005023
        DW      0005023
        DW      0010412
        DW      0010437
        DW      TQ_B_RING + 000006
        DW      0005711
        DW      0005712
        DW      0100776
        DW      0105737
        DW      TQ_B_TKRSP + 000012
        DW      0001401
        DW      0000000
        DW      0012703
        DW      TQ_B_TKCMD + 000010
        DW      0012723
        DW      0000041
        DW      0012723
        DW      0020000
        DW      0012723
        DW      0001000
        DW      0005023
        DW      0005023
        DW      0010412
        DW      0010437
        DW      TQ_B_RING + 000006
        DW      0005711
        DW      0005712
        DW      0100776
        DW      0105737
        DW      TQ_B_TKRSP + 000012
        DW      0001401
        DW      0000000
        DW      0005003
        DW      0012704
        DW      TQ_BOOT_ADDR + 000020
        DW      0005005
        DW      0005007
        DW      0100000
        DW      TQ_B_RING
        DW      0000000
        DW      0000001

stack_area:
        DS      128
stack_top:
