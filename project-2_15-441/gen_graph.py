#!/usr/bin/env python3
# Copyright (C) 2022 Carnegie Mellon University
#
# This file is part of the TCP in the Wild course project developed for the
# Computer Networks course (15-441/641) taught at Carnegie Mellon University.
#
# No part of the project may be copied and/or distributed without the express
# permission of the 15-441/641 course staff.

from scapy.all import rdpcap, Raw, IP
import matplotlib.pyplot as plt

# Change this to be your pcap file
# You can capture a pcap file with wireshark or tcpdump
# https://support.rackspace.com/how-to/capturing-packets-with-tcpdump/
FILE_TO_READ = "capture.pcap"

packets = rdpcap(FILE_TO_READ)
packet_list = []
times = []
base = 0
server_port = 15441
num_packets = 0

# This script assumes that only the client is sending data to the server.

for packet in packets:
    payload = packet[Raw].load

    if IP not in packet:
        continue

    if int.from_bytes(payload[:4], byteorder="big") != 15441:
        continue

    # Count the number of data packets going into the network.
    if packet[IP].dport == server_port:
        hlen = int.from_bytes(payload[16:18], byteorder="big")
        plen = int.from_bytes(payload[18:20], byteorder="big")
        if plen > hlen:  # Data packet
            num_packets = num_packets + 1
        time = packet.time
        if base == 0:
            base = time
        packet_list.append(num_packets)
        times.append(time - base)

    # Count the number of ACKs from server to client.
    elif packet[IP].sport == server_port:
        mask = int.from_bytes(payload[20:21], byteorder="big")
        if (mask & 4) == 4:  # ACK PACKET
            num_packets = max(num_packets - 1, 0)
        time = packet.time
        if base == 0:
            base = time
        packet_list.append(num_packets)
        times.append(time - base)


if __name__ == "__main__":
    # https://matplotlib.org/users/pyplot_tutorial.html for how to format and
    # make a good quality graph.
    print(packet_list)
    plt.scatter(times, packet_list)
    plt.savefig("graph.pdf")
