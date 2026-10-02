cd [file dirname [file normalize [info script]]]
if {[catch {
prj_project open hc7000-ps2-idle-02-20261002.ldf
prj_run Export -impl impl1 -task Jedecgen
prj_project close
} message]} {puts stderr $message; exit 1}
exit 0
