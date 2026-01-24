#!/usr/bin/env bash
# Build the static UDP client using musl-gcc
# This script assumes musl-tools is installed and that udp-client.c exists in the repository root.

set -e

# Find the source file (it may be in the client/ directory or at the repo root)
if [[ -f "client/udp-client.c" ]]; then
    SRC="client/udp-client.c"
elif [[ -f "udp-client.c" ]]; then
    SRC="udp-client.c"
else
    echo "Error: udp-client.c not found in repository." >&2
    exit 1
fi

# Output binary name (musl static)
OUT="client/udp-client.musl"

# Compile with musl-gcc (static linking)
if command -v musl-gcc >/dev/null 2>&1; then
    echo "Compiling $SRC with musl-gcc..."
    musl-gcc -static -Wall -Wextra -Os -s "$SRC" client/sha-256.c -o "$OUT"
    echo "Built $OUT"
else
    echo "musl-gcc not found. Please install musl-tools package." >&2
    exit 1
fi
