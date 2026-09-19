#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later OR MIT
"""Listen for the payload's UDP log and print it, optionally to a file."""

import socket
import sys
import time

DEFAULT_PORT = 9027
MAX_DATAGRAM = 65535
RECV_TIMEOUT_SEC = 1.0
QUIET_GAP_SEC = 1.0


def listen(sock, log_file):
    first_packet_time = None
    last_packet_time = None

    while True:
        try:
            data, addr = sock.recvfrom(MAX_DATAGRAM)
        except socket.timeout:
            continue

        now = time.time()
        if first_packet_time is None:
            first_packet_time = now
            print("--- first packet from %s:%d ---" % addr)

        gap_note = ""
        if (last_packet_time is not None
                and now - last_packet_time > QUIET_GAP_SEC):
            gap_note = "  (+%.1fs)" % (now - last_packet_time)
        last_packet_time = now

        text = data.decode("utf-8", errors="backslashreplace").rstrip("\r\n")
        for line in text.split("\n"):
            log_line = ("[%7.2fs]%s %s"
                        % (now - first_packet_time, gap_note, line))
            print(log_line, flush=True)
            if log_file:
                log_file.write(log_line + "\n")
                log_file.flush()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PORT
    outfile = sys.argv[2] if len(sys.argv) > 2 else None

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", port))
    sock.settimeout(RECV_TIMEOUT_SEC)

    log_file = open(outfile, "w", encoding="utf-8") if outfile else None
    print("listening on udp/%d%s (Ctrl+C to stop)"
          % (port, (" -> " + outfile) if outfile else ""))

    try:
        listen(sock, log_file)
    except KeyboardInterrupt:
        print("\nstopped.")
    finally:
        if log_file:
            log_file.close()
        sock.close()


if __name__ == "__main__":
    main()
