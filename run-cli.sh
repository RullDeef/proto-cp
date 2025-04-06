#!/bin/bash

# ensure latest build
cd build
ninja
cd ..

trap 'echo SIGINT' INT

# run as userN where N is passed as first argument

# local IPv4 address
address=127.0.0.100:$((20000+$1))
# UNIX socket
#address=file://user$1.sock

# regular run
./build/selecon_cli --listen-on $address

# valgrind run
#valgrind ./build/selecon_cli --listen-on $address

# gdb run
#gdb -ex 'set confirm off' -ex r -ex bt -ex q --args ./build/selecon_cli --listen-on $address

# cleanup socket file
rm -rf user$1.sock
