cd [file dirname [file normalize [info script]]]
set tool [file join $::env(FOUNDRY) bin lin64 trce]
if {[catch {exec $tool -v 10 -c -u 0 -gt -sethld -sp 4 -sphld m -o design.twr design.ncd design.prf 2>@1} message]} {
    puts stderr $message
    exit 1
}
puts $message
exit 0
