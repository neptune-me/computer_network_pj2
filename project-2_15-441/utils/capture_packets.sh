#!/usr/bin/env bash
# Copyright (C) 2022 Carnegie Mellon University
#
# This file is part of the TCP in the Wild course project developed for the
# Computer Networks course (15-441/641) taught at Carnegie Mellon University.
#
# No part of the project may be copied and/or distributed without the express
# permission of the 15-441/641 course staff.

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
HOST=$(hostname)
IFNAME=$(ifconfig | grep -B1 10.0.1. | grep -o "^\w*")
FUNCTION_TO_RUN=$1
PCAP_NAME=$2

if [ -z "$FUNCTION_TO_RUN" ]
    then
        echo "usage: ./capture_packets.sh < start | stop | analyze > PCAP_NAME"
        echo "Expecting name of function to run: start, stop, or analyze."
        exit 1
fi

if [ -z "$PCAP_NAME" ]
  then
    PCAP_NAME=$HOST.pcap
    echo NO PCAP_NAME PARAM SO USING DEFAULT FILE: $PCAP_NAME
fi

start() {
    sudo tcpdump -i $IFNAME -w $PCAP_NAME udp > /dev/null 2> /dev/null < \
        /dev/null &
}

stop() {
    sudo pkill -f "tcpdump -i $IFNAME -w $PCAP_NAME udp"
}

analyze() {
    tshark -X lua_script:$DIR/tcp.lua -R "cmutcp and not icmp" -r $PCAP_NAME \
    -T fields \
    -e frame.time_relative \
    -e ip.src \
    -e cmutcp.source_port \
    -e ip.dst \
    -e cmutcp.destination_port \
    -e cmutcp.seq_num \
    -e cmutcp.ack_num \
    -e cmutcp.hlen \
    -e cmutcp.plen \
    -e cmutcp.flags \
    -e cmutcp.advertised_window \
    -e cmutcp.extension_length \
    -E header=y -E separator=, \
    -2
}

$FUNCTION_TO_RUN
