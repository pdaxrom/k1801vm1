cd [file dirname [file normalize [info script]]]
if {[catch {
prj_project open hc7000-ps2-idle-02-20261002.ldf
prj_run Synthesis -impl impl1
prj_run Translate -impl impl1
prj_run Map -impl impl1
prj_run PAR -impl impl1
prj_run PAR -impl impl1 -task PARTrace
prj_project close
} message]} {puts stderr $message; exit 1}
exit 0
