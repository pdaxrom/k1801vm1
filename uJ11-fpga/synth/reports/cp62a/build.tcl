cd [file dirname [file normalize [info script]]]
if {[catch {
prj_project open cp62a.ldf
# UJ11_MMU undefined: MMU-less production profile
prj_run Synthesis -impl impl1
prj_run Translate -impl impl1
prj_run Map -impl impl1
set pref_path "impl1/cp62a_impl1.prf"
set fd [open $pref_path r]
set pref_data [read $fd]
close $fd
file copy -force $pref_path mapped-original.prf
if {[regsub -all {FREQUENCY NET "clk" [0-9.]+ MHz ;} $pref_data {FREQUENCY NET "clk" 31.824000 MHz ;} pref_data] != 1} {error "Expected exactly one mapped OSCH frequency"}
set fd [open "/tmp/uj11-cp61-20260912/uJ11-fpga/synth/machxo2/fram-timing.lpf" r]
append pref_data "\n" [read $fd]
close $fd

set fd [open $pref_path w]
puts -nonewline $fd $pref_data
close $fd

prj_run PAR -impl impl1
prj_run PAR -impl impl1 -task PARTrace
prj_project close
} message]} {puts stderr $message; exit 1}
exit 0
