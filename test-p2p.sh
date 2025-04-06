#/bin/env sh

# test peer to peer conference using unix socket files

CLI=./build/selecon_cli

cat <<EOF | ${CLI} --listen-on file://user1.sock --user user1 &
dump
sleep 8
EOF

sleep 3

cat <<EOF | ${CLI} --listen-on file://user2.sock --user user2
dump
invite file://user1.sock
sleep 5
EOF

rm user1.sock user2.sock
