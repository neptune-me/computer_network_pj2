#!/usr/bin/env python3
# Copyright (C) 2022 Carnegie Mellon University
#
# This file is part of the TCP in the Wild course project developed for the
# Computer Networks course (15-441/641) taught at Carnegie Mellon University.
#
# No part of the project may be copied and/or distributed without the express
# permission of the 15-441/641 course staff.

from fabric import Connection
from scapy.all import sr1, Raw

from common import (
    CMUTCP,
    ACK_MASK,
    TIMEOUT,
    IFNAME,
    SYN_MASK,
    START_TESTING_SERVER_CMD,
    STOP_TESTING_SERVER_CMD,
    TESTING_HOST_IP,
    ip,
    udp,
)

payloads = ["pa", "pytest 1234567"]


def test_basic_ack_packets():
    print("Running test_basic_ack_packets()")
    """Basic test: Check if when you send data packets, the server responds
    with correct ack packet with a correct ack number.
    """
    print("Running test_sequence_number()")
    for payload in payloads:
        print("Testing payload size " + str(len(payload)))
        with Connection(
            host=TESTING_HOST_IP,
            user="vagrant",
            connect_kwargs={"password": "vagrant"},
        ) as conn:
            try:
                conn.run(START_TESTING_SERVER_CMD)
                conn.run("tmux has-session -t pytest_server")
                syn_pkt = (
                    ip /
                    udp /
                    CMUTCP(plen=25, seq_num=1000, flags=SYN_MASK)
                )
                syn_ack_pkt = sr1(syn_pkt, timeout=TIMEOUT, iface=IFNAME)

                if (
                    syn_ack_pkt is None
                    or syn_ack_pkt[CMUTCP].flags != SYN_MASK | ACK_MASK
                    or syn_ack_pkt[CMUTCP].ack_num != 1000 + 1
                ):
                    print(
                        "Listener (server) did not properly respond to SYN "
                        "packet."
                    )
                    print("Test Failed")
                    conn.run(STOP_TESTING_SERVER_CMD)
                    return

                print(syn_ack_pkt[CMUTCP].seq_num)

                ack_pkt = (
                    ip /
                    udp /
                    CMUTCP(
                        plen=25,
                        seq_num=1001,
                        ack_num=syn_ack_pkt[CMUTCP].seq_num + 1,
                        flags=ACK_MASK,
                    )
                )
                empty_pkt = sr1(ack_pkt, timeout=0.5, iface=IFNAME)

                if empty_pkt is not None:
                    print("Listener (server) should not respond to ack pkt.")
                    print("Test Failed")
                    conn.run(STOP_TESTING_SERVER_CMD)
                    return

                data_pkt = (
                    ip /
                    udp /
                    CMUTCP(
                        plen=25 + len(payload),
                        seq_num=1001,
                        ack_num=syn_ack_pkt[CMUTCP].seq_num + 1,
                        flags=ACK_MASK,
                    )
                    / Raw(load=payload)
                )

                server_ack_pkt = sr1(data_pkt, timeout=TIMEOUT, iface=IFNAME)

                if (
                    server_ack_pkt is None
                    or server_ack_pkt[CMUTCP].flags != ACK_MASK
                    or server_ack_pkt[CMUTCP].ack_num != 1001 + len(payload)
                ):
                    print(
                        "Listener (server) did not properly respond to data "
                        "packet."
                    )
                    print("Test Failed")
                    conn.run(STOP_TESTING_SERVER_CMD)
                    return

            finally:
                try:
                    conn.run(STOP_TESTING_SERVER_CMD)
                except Exception:
                    # Ignore error here that may occur if server stopped.
                    pass
            print("Test Passed")


if __name__ == "__main__":
    test_basic_ack_packets()
