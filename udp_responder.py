#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
udp_responder.py
~~~~~~~~~~~~~~~~
A tiny UDP “server” for Python 2.7 that reads a single request/reply pair
from a two‑line text file.

Behaviour (as requested):
* On start‑up prints one line:  "Listening on :"
* When a packet matches the trigger, prints one line: "Replied to :"
* Nothing is printed for non‑matching packets.
* The trigger and reply strings are never displayed.
* Handles SIGTERM (graceful stop) and SIGHUP (reload pair file) – suitable
  for running as a system service (systemd, upstart, Docker, etc.).

Author: ChatGPT
"""

from __future__ import print_function
import argparse
import socket
import sys
import os
import signal

# ----------------------------------------------------------------------
# Global state that the signal handlers will modify
# ----------------------------------------------------------------------
running = True          # main loop checks this
trigger_bytes = None    # set by load_pair()
reply_bytes   = None    # set by load_pair()
sock          = None    # the UDP socket (created once)


# ----------------------------------------------------------------------
def parse_args():
    """Parse command‑line arguments."""
    parser = argparse.ArgumentParser(
        description='UDP listener that replies with a fixed message '
                    'read from a two‑line file when a specific trigger '
                    'string is received.  No secret data is printed.'
    )
    parser.add_argument(
        '--listen-port', '-p',
        type=int,
        default=51818,
        help='UDP port on which to listen (default: 51818)'
    )
    parser.add_argument(
        '--listen-addr', '-a',
        default='0.0.0.0',
        help='IP address to bind to (default: all interfaces, 0.0.0.0)'
    )
    parser.add_argument(
        '--pair-file', '-f',
        required=True,
        help='Path to a two‑line file: line 1 = trigger, line 2 = reply.'
    )
    parser.add_argument(
        '--buffer-size', '-b',
        type=int,
        default=4096,
        help='Maximum UDP payload size to accept (default: 4096 bytes)'
    )
    return parser.parse_args()


# ----------------------------------------------------------------------
def load_pair(file_path):
    """
    Read the trigger and reply from *file_path*.
    Returns (trigger, reply) as plain strings (bytes in Python 2).

    Exits with an error message if the file is missing, unreadable,
    or does not contain exactly two non‑empty lines.
    """
    if not os.path.isfile(file_path):
        sys.stderr.write('Error: pair file not found – {}\n'.format(file_path))
        sys.exit(1)

    try:
        with open(file_path, 'r') as f:
            lines = f.readlines()
    except IOError as e:
        sys.stderr.write('Error reading {}: {}\n'.format(file_path, e))
        sys.exit(1)

    # Strip only line terminators; keep any other whitespace.
    stripped = [line.rstrip('\r\n') for line in lines]

    # Discard trailing empty lines that may appear when the file ends with a newline.
    while stripped and stripped[-1] == '':
        stripped.pop()

    if len(stripped) != 2:
        sys.stderr.write(
            'Error: the pair file must contain exactly two non‑empty lines.\n'
            'Found {} line(s).\n'.format(len(stripped))
        )
        sys.exit(1)

    return stripped[0], stripped[1]


# ----------------------------------------------------------------------
def init_socket(listen_addr, listen_port):
    """Create, bind and return a UDP socket."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((listen_addr, listen_port))
        return s
    except socket.error as e:
        sys.stderr.write('Failed to bind UDP socket on {}:{} – {}\n'.format(
            listen_addr, listen_port, e))
        sys.exit(1)


# ----------------------------------------------------------------------
# Signal handlers -------------------------------------------------------
def handle_sigterm(signum, frame):
    """SIGTERM – ask the main loop to exit."""
    global running
    running = False   # the main loop will notice and break


def handle_sighup(signum, frame):
    """
    SIGHUP – reload the trigger/reply pair file.
    This is optional but handy when you want to change the secret
    without restarting the service.
    """
    global trigger_bytes, reply_bytes
    try:
        t, r = load_pair(args.pair_file)
        trigger_bytes, reply_bytes = t, r
        # No console output – we keep the secret hidden.
    except Exception as e:
        # We cannot raise; just write to stderr so the admin can see the problem.
        sys.stderr.write('Failed to reload pair file on SIGHUP: {}\n'.format(e))


# ----------------------------------------------------------------------
def main():
    global args, sock, trigger_bytes, reply_bytes, running

    args = parse_args()

    # Load the secret pair for the first time.
    trigger_bytes, reply_bytes = load_pair(args.pair_file)

    # Create and bind the socket.
    sock = init_socket(args.listen_addr, args.listen_port)

    # Print the single “listening” line.
    print('Listening on {}:{}'.format(args.listen_addr, args.listen_port))

    # Register signal handlers **after** the socket is created so that
    # they can safely reference global objects.
    signal.signal(signal.SIGTERM, handle_sigterm)   # normal stop
    signal.signal(signal.SIGHUP,  handle_sighup)    # optional reload

    # ------------------------------------------------------------------
    # Main receive / reply loop
    # ------------------------------------------------------------------
    while running:
        try:
            # recvfrom will block; we rely on the signal handler to set
            # `running = False` which will cause us to break out after the
            # current recv finishes (or we can use a timeout – see note below).
            data, addr = sock.recvfrom(args.buffer_size)
        except socket.error:
            # In a production daemon you might log this; we ignore it here.
            continue
        except KeyboardInterrupt:
            # If somebody *does* run this from a terminal, treat it like SIGTERM.
            running = False
            continue

        # If the payload matches the secret trigger, send the reply.
        if data == trigger_bytes:
            try:
                sock.sendto(reply_bytes, addr)
                # Only the remote address is printed – nothing else leaks.
                print('Replied to {}:{}'.format(addr[0], addr[1]))
            except socket.error:
                # Silently ignore send failures; a real daemon would log.
                pass
        # Non‑matching packets stay silent.

    # ------------------------------------------------------------------
    # Clean shutdown
    # ------------------------------------------------------------------
    sock.close()
    # The script exits silently (no extra line) – the service manager will
    # consider the process finished.
    # If you want a final log line, uncomment the next line:
    # print('udp_responder stopped.')

if __name__ == '__main__':
    main()

