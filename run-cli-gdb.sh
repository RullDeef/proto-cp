#!/bin/bash

# ensure latest build
cd build
ninja
cd ..

trap 'echo SIGINT' INT

# run as userN where N is passed as first argument

# regular run
#./build/selecon_cli --listen-on file://user$1.sock

# valgrind run
#valgrind ./build/selecon_cli --listen-on file://user$1.sock

# gdb run
gdb -ex 'set confirm off' -ex r -ex bt -ex q --args ./build/selecon_cli --listen-on file://user$1.sock

# cleanup socket file
rm user$1.sock
