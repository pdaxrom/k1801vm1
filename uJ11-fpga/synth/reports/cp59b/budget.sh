set -e
bindir=/home/sash/.local/lscc/diamond/3.14/bin/lin64
source "$bindir/diamond_env"
export LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6
trce -v 100 -c -u 0 -sethld -sp 4 -sphld m -o /tmp/uj11-cp59-20260912/uJ11-fpga/build/cp59-io/budget.twr /tmp/uj11-cp59-20260912/uJ11-fpga/build/cp59b/impl1/cp59b_impl1.ncd /tmp/uj11-cp59-20260912/uJ11-fpga/build/cp59-io/budget.prf
