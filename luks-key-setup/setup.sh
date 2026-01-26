#!/usr/bin/env bash
# Interactive setup for LUKS unlock via UDP token.
# This script creates the static secret, generates the keyscript,
# builds the static udp-client binary, installs the initramfs hook,
# and updates the initramfs image.
# It does NOT modify LUKS volumes – you must add the keyscript entry to /etc/crypttab manually.

set -e

# ---------- 1. Gather parameters ----------
# Load existing static key and trigger if present
if sudo test -f /etc/initramfs-tools/conf.d/unattended-cryptsetup-keys; then
    echo "Loading existing secrets from unattended-cryptsetup-keys..."
    eval "$(sudo cat /etc/initramfs-tools/conf.d/unattended-cryptsetup-keys)"
fi

read -p "UDP server IP address: " SERVER_IP
read -p "UDP server port (default 51818): " SERVER_PORT
SERVER_PORT=${SERVER_PORT:-51818}
# Prompt for trigger, default to existing $TRIGGER if set
if [ -n "${TRIGGER:-}" ]; then
    read -p "UDP trigger string (sent to server, e.g., 'unlock') [default: $TRIGGER]: " TRIGGER_INPUT
    TRIGGER=${TRIGGER_INPUT:-$TRIGGER}
else
    read -p "UDP trigger string (sent to server, e.g., 'unlock'): " TRIGGER
fi
# Prompt for static secret, default to existing $STATIC_KEY if set
if [ -n "${STATIC_KEY:-}" ]; then
    read -p "Static secret (will be stored inside initramfs) [default: $STATIC_KEY]: " STATIC_INPUT
    STATIC_SECRET=${STATIC_INPUT:-$STATIC_KEY}
else
    read -p "Static secret (will be stored inside initramfs): " STATIC_SECRET
fi
read -p "Volume device to unlock (e.g., /dev/sda3) [optional, for your notes]: " VOLUME

# ---------- 2. Write static secret into initramfs config ----------
echo "Writing static secret and trigger to /etc/initramfs-tools/conf.d/unattended-cryptsetup-keys (requires sudo)..."
printf "STATIC_KEY=%s\nTRIGGER=%s\n" "$STATIC_SECRET" "$TRIGGER" | sudo tee /etc/initramfs-tools/conf.d/unattended-cryptsetup-keys > /dev/null
sudo chmod 600 /etc/initramfs-tools/conf.d/unattended-cryptsetup-keys

# ---------- 3. Install the keyscript with the supplied values ----------
KEYSCRIPT_TMP=$(mktemp)
cp "$(dirname "$0")/udp_keyscript" "$KEYSCRIPT_TMP"
sed -i "s|{{SERVER_IP}}|$SERVER_IP|g" "$KEYSCRIPT_TMP"
sed -i "s|{{SERVER_PORT}}|$SERVER_PORT|g" "$KEYSCRIPT_TMP"
# TRIGGER is now read from secret file, no replacement needed
sudo install -m 0755 "$KEYSCRIPT_TMP" /usr/lib/cryptsetup/scripts/udp_keyscript
rm "$KEYSCRIPT_TMP"

# ---------- 4. Update initramfs-tools build script ----------
# Install the initramfs hook (will copy binary & keyscript into the initramfs image)
sudo install -m 0755 ./luks-key-setup/udp_client_hook /etc/initramfs-tools/hooks/udp_client_hook

# ---------- 5. Follow-up steps ----------
echo -e "\nSetup complete.\n"

if [[ -n "$VOLUME" ]]; then
    echo "To add the fetched key to LUKS volume $VOLUME, run the following command (replace with your actual device if different):"
    echo ''
    echo "sudo cryptsetup luksAddKey $VOLUME --key-file - <<'EOF'"
    echo "echo -n \"$TRIGGER\" | nc -u -w 5 \"$SERVER_IP\" \"$SERVER_PORT\" | { printf '%s' \"$STATIC_SECRET\"; cat; } | sha256sum | awk '{print \\\$1}' | xxd -r -p"
    echo "EOF"
else
    echo "Add a line to /etc/crypttab pointing to the keyscript you just installed."
fi

echo ''

echo "Initramfs needs to be regenerated to include the new hook and key file."
echo "After you have added the appropriate line to /etc/crypttab, run the following command:"
echo "sudo update-initramfs -u -k \`uname -r\`"
