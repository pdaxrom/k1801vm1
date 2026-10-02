!.MODULE STARTF,01,<FB Startup command file>
TYPE V5USER.TXT
!
! Typical commands to control system startup for a distributed FB monitor.
!
! Don't let anybody see what we're doing.
!
!SET TT QUIET
!
! Set terminal characteristics if not a scope (ie, LA100, LA120 type terminals)
! VT100, VT200 or PC300 series users should NOT select the following command.
!
!SET TT NOSCOPE,NOTAB,FORM
!
! Select the editor of your choice. Take the default (KED) unless you are
! not using a VT100 compatible terminal.  If you are using an incompatible
! terminal (or a hard copy terminal) select the following command.
!
!SET EDIT EDIT
!
! Turn the single line editor on.  The LET command is an example of a usefull
! RT-11 substitution but should only be used if the ";" character is not used.
!
!SET SL LET,KMON
!LET _;=:
!
! Start the communications program on a PDP-11.
!
!	VTCOM may run as a foreground or system job.  If run as a foreground
!	job, the following examples apply.  If run as a system job, the
!	LOAD and RESUME must be altered to specify the job name, VTCOM.
!
!FRUN SY:VTCOM/PAUSE
!LOAD XL=F
!RESUME
!
! Start the communications program on a Pro 300.
!
!FRUN SY:VTCOM/PAUSE
!LOAD XC=F
!RESUME
!
! Start the spooler on a Pro 300 or a PDP-11 with a serial printer.
!
!	SPOOL may run as a foreground or system job.  If run as a foreground
!	job, the following examples apply.  If run as a system job, the
!	LOAD and RESUME must be altered to specify the job name, SPOOL.
!
!SET USR NOSWAP
!FRUN SPOOL/PAUSE/BUFFER:256.
!LOAD LS=F
!RESUME
!ASS SP0 LP
!ASS SP0 LP0
!ASS SP0 LS
!ASS SP0 LS0
!SET USR SWAP
!
! Start the spooler on a PDP-11 with a line printer.
!
!SET USR NOSWAP
!FRUN SPOOL/PAUSE/BUFFER:256.
!LOAD LP=F
!RESUME
!ASS SP0 LP
!ASS SP0 LP0
!SET USR SWAP
!
! Load the DU handler to prevent reinitializations (if you have one). Do
! NOT select this option on a Pro 300 or a Micro/PDP-11.
!
!LOAD DU
!
! Load the LD handler as required (usually better if it is default disk).
!
!LOAD LD
!ASSI LD0 DK
!
! Use VM for scratch and work file space.  Do NOT select this unless you
! are programming in the MACRO language and have a mapped processor with
! enough memory to contain the scratch files in VM.
!
!INITIALIZE/NOQUERY VM:
!ASS VM0 CF
!ASS VM0 WF
!
! Clear display and show memory.
!
!SETUP VT100,JUMP,DARK,CLEAR
!SHOW MEMORY
!DATE
!SET TT NOQUIET
ASSIGN DM1: VOL:
ASSIGN RK1: BAS:
ASSIGN RK2: PAS:
ASSIGN RK3: FOR:
