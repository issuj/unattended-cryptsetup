#!/usr/bin/env bash
# Interactive setup for LUKS unlock via UDP token.
# This script creates the static secret, generates the keyscript,
# builds the static udp-client binary, installs the initramfs hook,
# and updates the initramfs image.
# It does NOT modify LUKS volumes – you must add the keyscript entry to /etc/crypttab manually.

set -e

# ---------- 1. Gather parameters ----------
read -p "UDP server IP address: " SERVER_IP
read -p "UDP server port (default 51818): " SERVER_PORT
SERVER_PORT=${SERVER_PORT:-51818}
read -p "UDP trigger string (sent to server, e.g., 'unlock'): " TRIGGER
read -p "Static secret (will be stored inside initramfs): " STATIC_SECRET
read -p "Volume device to unlock (e.g., /dev/sda3) [optional, for your notes]: " VOLUME

# ---------- 2. Write static secret into initramfs config ----------
echo "Writing static secret to /etc/initramfs-tools/conf.d/static_key (requires sudo)..."
printf "%s\n" "$STATIC_SECRET" | sudo tee /etc/initramfs-tools/conf.d/static_key > /dev/null
sudo chmod 600 /etc/initramfs-tools/conf.d/static_key

# ---------- 3. Generate the keyscript with the supplied values ----------
KEYSCRIPT_TMP=$(mktemp)
cat <<'EOS' > "$KEYSCRIPT_TMP"
#!/bin/sh
# Auto‑generated udp_keyscript for cryptsetup
SERVER_IP="{{SERVER_IP}}"
SERVER_PORT="{{SERVER_PORT}}"
TRIGGER="{{TRIGGER}}"
STATIC_KEY_FILE="/etc/initramfs-tools/conf.d/static_key"

# Fetch token from UDP server
TOKEN=$(/usr/bin/udp-client "$SERVER_IP" "$SERVER_PORT" "$TRIGGER" 3 2000)
if [ $? -ne 0 ] || [ -z "$TOKEN" ]; then
    echo "Failed to obtain token from $SERVER_IP:$SERVER_PORT" >&2
    exit 1
fi

# Read static secret
if [ ! -r "$STATIC_KEY_FILE" ]; then
    echo "Static key file missing: $STATIC_KEY_FILE" >&2
    exit 1
fi
STATIC_KEY=$(cat "$STATIC_KEY_FILE")

# Derive final key (SHA‑256 of static+token, binary output)
printf "%s%s" "$STATIC_KEY" "$TOKEN" | sha256sum -b | awk '{print $1}' | xxd -r -p

exit 0
EOS

# Replace placeholders
sed -i "s|{{SERVER_IP}}|$SERVER_IP|g" "$KEYSCRIPT_TMP"
sed -i "s|{{SERVER_PORT}}|$SERVER_PORT|g" "$KEYSCRIPT_TMP"
sed -i "s|{{TRIGGER}}|$TRIGGER|g" "$KEYSCRIPT_TMP"

# Install the keyscript to the location cryptsetup expects (will be copied into initramfs by the hook)
sudo install -m 0755 "$KEYSCRIPT_TMP" /usr/lib/cryptsetup/scripts/udp_keyscript
rm "$KEYSCRIPT_TMP"

# ---------- 4. Build the static udp-client binary ----------
echo "Building static udp-client binary..."
# Assume this script lives in <repo>/luks-key-setup, so go one level up to repo root
cd "$(dirname "$0")/.."
./luks-key-setup/build_udp_client.sh

# ---------- 5. Install binary and initramfs hook ----------
# Install binary where the hook expects it (the hook copies it from the repo directory)
sudo install -m 0755 ./luks-key-setup/udp-client.musl /usr/local/sbin/udp-client

# Install the initramfs hook (will copy binary & keyscript into the initramfs image)
sudo install -m 0755 ./luks-key-setup/udp_client_hook /etc/initramfs-tools/hooks/udp-client

# ---------- 6. Update initramfs ----------
echo "Updating initramfs..."
sudo update-initramfs -u -k $(uname -r)

# ---------- 7. Final notes ----------
echo "\nSetup complete.\n"
if [[ -n "$VOLUME" ]]; then
    echo "Remember to add the following line to /etc/crypttab (replace UUID as needed):"
    echo "cryptroot UUID=$(blkid -s UUID -o value $VOLUME) none luks,keyscript=/usr/lib/cryptsetup/scripts/udp_keyscript"
else
    echo "Add a line to /etc/crypttab pointing to the keyscript you just installed."
fi

echo "You can now test the unlock by rebooting the machine."
