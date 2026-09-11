cd [file dirname [file normalize [info script]]]
if {[catch {
prj_project open cp54b.ldf
prj_run Export -impl impl1 -task Jedecgen
prj_project close
} message]} {puts stderr $message; exit 1}
exit 0
