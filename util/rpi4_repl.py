#!/usr/bin/env python3

"""Drives the Pi 4's serial toplevel from this machine.

The board's REPL echoes what it receives and answers at a `?- ` prompt, so a
goal can be sent and its reply read without anyone sitting at a terminal.
That matters for anything needing the host to act while the board is
listening - reading a register, putting frames on the wire, reading it again -
where a person relaying between two windows leaves gaps big enough to lose
the answer in.

    python3 util/rpi4_repl.py 'X is 6*7.' 'net_link(S).'
    python3 util/rpi4_repl.py --ping 192.168.50.2 'genet_reg(0x3008,V).'

The port takes one owner at a time: quit screen first.
"""

import argparse
import os
import select
import subprocess
import sys
import termios
import time

DEVICE = "/dev/cu.usbserial-0001"
PROMPT = b"?- "


class Board:
    def __init__(self, device=DEVICE, baud=115200):
        self.fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = attrs[1] = attrs[3] = 0		# raw: no translation either way
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[4] = attrs[5] = baud
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

    def close(self):
        os.close(self.fd)

    def read(self, seconds):
        """Everything that arrives within `seconds`."""
        got = b""
        end = time.time() + seconds

        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.1)

            if not ready:
                continue

            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:		# select can say ready and read still EAGAIN
                continue

            if chunk:
                got += chunk

        return got

    def wait_for_prompt(self, seconds=10):
        """Reads until the toplevel is idle, so a reply cannot be mistaken
        for the tail of the previous one."""
        got = b""
        end = time.time() + seconds

        while time.time() < end:
            got += self.read(0.2)

            if got.endswith(PROMPT):
                return got

        return got

    def send(self, goal, settle=2.0):
        """Sends one goal and returns what came back, the echo included.

        A byte at a time with a pause: the board echoes and parses in the
        same loop, and there is no flow control to lean on."""
        if not goal.endswith("."):
            goal += "."

        for byte in goal.encode() + b"\r":
            os.write(self.fd, bytes([byte]))
            time.sleep(0.006)

        reply = self.wait_for_prompt(settle + 8)
        return reply.decode("utf-8", "replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("goals", nargs="*", help="goals to run, in order")
    parser.add_argument("--device", default=DEVICE)
    parser.add_argument("--ping", metavar="ADDR",
        help="ping this address between goals, to give the board traffic")
    parser.add_argument("--count", type=int, default=5, help="pings to send")
    parser.add_argument("--wake", action="store_true",
        help="send a bare newline first, to clear a half-typed term")
    args = parser.parse_args()

    try:
        board = Board(args.device)
    except OSError as error:
        print(f"cannot open {args.device}: {error}", file=sys.stderr)
        print("quit screen first - the port takes one owner", file=sys.stderr)
        return 1

    try:
        board.read(0.5)					# discard anything already in flight

        if args.wake:
            os.write(board.fd, b"\r")
            board.wait_for_prompt(3)

        for i, goal in enumerate(args.goals):
            if args.ping and i:
                subprocess.run(["ping", "-c", str(args.count), "-i", "0.3",
                    "-W", "1000", args.ping], stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL, check=False)

            print(f"--- {goal}")
            print(board.send(goal).strip())
    finally:
        board.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
