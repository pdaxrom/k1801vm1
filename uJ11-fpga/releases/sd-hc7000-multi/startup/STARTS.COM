!.MODULE STARTS,01,<BL/SJ Startup command file)
!
! Select the editor of your choice. Take the default (KED) unless you are
! not using a VT100 compatible terminal.  If you are using an incompatible
! terminal (or a hard copy terminal) select the following command.
!
!SET EDIT EDIT
ASSIGN DM1: VOL:
ASSIGN RK1: BAS:
ASSIGN RK2: PAS:
ASSIGN RK3: FOR:
SET tt: scope
