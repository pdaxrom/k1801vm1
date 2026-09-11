cd [file dirname [file normalize [info script]]]
if {[catch {
prj_project open cp53c.ldf
# UJ11_MMU undefined: MMU-less production profile
prj_run Synthesis -impl impl1
prj_run Translate -impl impl1
prj_run Map -impl impl1
prj_run PAR -impl impl1
prj_run PAR -impl impl1 -task PARTrace
prj_project close
} message]} {puts stderr $message; exit 1}
exit 0
