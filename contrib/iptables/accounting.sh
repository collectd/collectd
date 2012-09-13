#!/bin/bash
#Simple script that sets up some chains in mangle table to do global logging of all 
#traffic going in and out of an interface
#Could also use the regular input/output tree but this also catches the forwarded nat traffic

IT="iptables -t mangle"

#First clear the old stuff
$IT -F incoming
$IT -F outgoing
$IT -N incoming
$IT -N outgoing

$IT -D PREROUTING -i eth0 -j incoming
$IT -D POSTROUTING -o eth0 -j outgoing

#should add some arg == stop exit here...

$IT -A PREROUTING -i eth0 -j incoming
$IT -A POSTROUTING -o eth0 -j outgoing

$IT -A incoming -p tcp -m comment --comment "tcp"
$IT -A incoming -p udp -m comment --comment "udp"
$IT -A incoming -p icmp -m comment --comment "icmp"

$IT -A outgoing -p tcp -m comment --comment "tcp"
$IT -A outgoing -p udp -m comment --comment "udp"
$IT -A outgoing -p icmp -m comment --comment "icmp"

