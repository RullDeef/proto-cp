#/bin/env sh

# test peer to peer conference using unix socket files

CLI=./build/selecon_cli

cat <<EOF | ${CLI} --listen-on file://user1.sock --user user1 &
dump
EOF

sleep 1

cat <<EOF | ${CLI} --listen-on file://user2.sock --user user2
dump
invite file://user1.sock
EOF

rm user1.sock user2.sock
