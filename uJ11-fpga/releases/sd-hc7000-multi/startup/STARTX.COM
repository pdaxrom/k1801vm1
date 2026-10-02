!.MODULE STARTX,01,<XM Startup command file>
TYPE V5USER.TXT
!
! Typical commands to control system startup of a distributed XM monitor.
!
! Don't let anybody see what we're doing.
!
!SET TT QUIET
!
! Select the terminal characteristics (if not a scope).  Assumes an LA100
! or LA120 type terminal. Note that the distributed XM monitor comes pre-
! set in scope mode.  Do NOT select the next line unless you have a hard-
! copy terminal. Do NOT select the next line on a PC300 system.
!
!SET TT NOSCOPE,NOTAB,FORM,NOCRLF
!
! Select the editor of your choice. Take the default (KEX) unless you are
! not using a VT100 compatible terminal.  If you are using an incompatible
! terminal (or a hard copy terminal) select the following command.
!
!SET EDIT EDIT
!
! Turn the single line editor on.  Do not select this option unless you
! have a scope type terminal or a Professional 300 series system.   The
! associated LET command is an example of a usefull RT-11 substitution.
! Normally, Pro 300 users select the next two lines.
!
!SET SL LET,KMON
!LET _;=:
!
! RT-11 systems can be used as a "terminal" when connected via a serial
! interface to a host computer. If you are connected to a host computer
! select the communications handler appropriate for your system.
!
! For PDP-11 systems....
! Note that the XL handler  must  have  its  CSR  and vector correctly
! selected by using SET XL CSR=nnnnnn and SET XL VECTOR=nnn before the
! following commands will work.  XL is distributed with CSR=176500 and
! vector=300.
!
!FRUN SY:VTCOM.SAV/PAUSE
!LOAD XL=VTCOM
!RESUME VTCOM
!
! For the Professional 300....
! Note that the XC handler must have its  speed correctly selected by
! using SET XC SPEED=nnnn before the following will work.
!
!FRUN SY:VTCOM.SAV/PAUSE
!LOAD XC=VTCOM
!RESUME VTCOM
!
! Start the spooler on a Pro 300  or  a  PDP-11 with a serial printer.
! On PDP-11 systems,  the LS handler must have its CSR and vector cor-
! rectly selected using the  SET LS CSR=nnnnnn  and  SET LS VECTOR=nnn
! commands. LS is distributed with its CSR and vector preset for a Pro
! 300 computer. If you are a Professional 350 user and have a printer,
! the following commands are normally selected.
!
!SRUN SPOOL/PAUSE
!LOAD LS=SPOOL
!RESUME SPOOL
!ASS SP0 LP
!ASS SP0 LP0
!ASS SP0 LS
!ASS SP0 LS0
!
! Start the spooler on a PDP-11 with a line printer.
!
!SRUN SPOOL/PAUSE
!LOAD LP=SPOOL
!RESUME SPOOL
!ASS SP0 LP
!ASS SP0 LP0
!
! Load the DU device to prevent re-initialization every time the DU
! handler is fetched. This should be done only if your DU device is
! other than the system disk.     Professional 300 users should NOT
! select the following command.       Micro/PDP-11 users should not
! select the following  command  unless  they are booted from other
! than the RX50 or RD51 devices.
!
!LOAD DU
!
! If logical disks are to be utilized,  it is usually better if the
! logical disk handler  is  loaded.   This improves performance and
! is more convenient if  foreground  and/or system jobs are active.
! Pro 300 and Micro/PDP-11 users normally select the following.
!
!LOAD LD
!
! Use VM for scratch and work file space.   If you are a MACRO user
! user and have sufficient  memory,  using  VM  for scratch storage
! will increase system performance.
!
!INITIALIZE/NOQUERY VM:
!ASS VM0 CF
!ASS VM0 WF
!
! If you are a Pro 300 user and have a color monitor, you may select
! the appropriate background and character colors.   Do  NOT  select
! the following commands if  you  do not have a Pro 300 with a color
! monitor. White on blue looks nice.
!
!SETUP WHITE ON BLUE
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
