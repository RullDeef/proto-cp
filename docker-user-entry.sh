#!/bin/bash

# expects env GW_IP to be default gateway ip
ip route del default
ip route add default via $GW_IP
ip r

MY_IP=$(ip a
  | grep -Eo 'inet (addr:)?([0-9]*\.){3}[0-9]*'
  | grep -Eo '([0-9]*\.){3}[0-9]*'
  | grep -v '127.0.0.1')

/app/selecon --listen-on $MY_IP:11235 -u $USERNAME
