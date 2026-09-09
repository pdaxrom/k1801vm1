# CP1 implementation only; never invokes Programmer or exports a board image.
cd [file dirname [file normalize [info script]]]
if {[file exists impl1]} {
    puts stderr "Fresh CP1 build required: retain/move impl1 before rerunning."
    exit 1
}
if {[catch {
    prj_project open uj11-seq.ldf
    prj_run Synthesis -impl impl1
    prj_run Translate -impl impl1
    prj_run Map -impl impl1
    prj_run PAR -impl impl1
    prj_run PAR -impl impl1 -task PARTrace
    prj_project close
} message]} {
    puts stderr "uJ11 CP1 build failed: $message"
    exit 1
}
exit 0
