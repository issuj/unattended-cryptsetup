# UDP‑Based LUKS Unlock Setup

## Summary

This directory now provides a **self‑contained automation** that lets an Ubuntu 24.04 machine unlock its LUKS‑encrypted root volume **without any manual password entry**. The unlock works by:

1. **Fetching a secret** from a UDP server you run on an EdgeRouter (or any reachable host).
2. **Mixing that token** with a **static secret** baked into the initramfs image.
3. **Hashing the combination** with SHA‑256 to produce the 256‑bit key that `cryptsetup` uses to open the LUKS container.

All the heavy lifting now happens **inside the initramfs** using standard utilities (`nc`, `sha256sum`, `awk`, `xxd`) instead of a custom static UDP client binary. A configurable retry loop ensures the request is re‑tried on failure.

> **Note:** The script does **not** modify your LUKS keyslots. You must add the generated key‑script entry to `/etc/crypttab` manually (the installer prints the exact line for you).

---

## How‑to‑Use (step‑by‑step)

1. **Make the helper scripts executable**
   ```bash
   chmod +x luks-key-setup/*.sh
   ```

2. **Run the interactive installer** (needs sudo for system writes):
   ```bash
   sudo ./luks-key-setup/setup.sh
   ```
   You will be prompted for:
   - UDP server IP address
   - UDP server port (default 51818)
   - Trigger string that the server expects (e.g. `unlock`)
   - A static secret (kept inside the initramfs)
   - (Optional) the block device you intend to unlock (e.g. `/dev/sda3`).

2.5 **Human intervention to AI slop confidence**
   **I recommend creating a backup of your initramfs at this point, or alternatively have some other way to boot into your system, in case your boot no longer works.**
   You also need to take steps to have your initramfs to get network connectivity, that's not covered by the script here. That is the part that's likely to fail in various ways.

3. **What the installer does**
   - Stores the static secret and trigger in `/etc/initramfs-tools/conf.d/unattended-cryptsetup-keys` (packed into the initramfs).
   - Generates a **key‑script** (`/usr/lib/cryptsetup/scripts/udp_keyscript`) that uses `nc -u -w "$TIMEOUT"` piped through `sha256sum`, `awk`, and `xxd`.
   - Adds **retry logic** (`RETRIES` and `TIMEOUT` variables) so the script will attempt the UDP request multiple times before falling back to a password prompt.
   - Installs an **initramfs‑tools hook** (`udp_client_hook`) that copies the required utilities (`nc`, `sha256sum`, `awk`, `xxd`) and the key‑script into the initramfs image.
   - Regenerates the initramfs for the currently running kernel.
   - Prints the line you need to add to `/etc/crypttab`.

4. **Add the key‑script entry to `/etc/crypttab`** (example output from the installer):
   ```text
   cryptroot UUID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx none luks,keyscript=/usr/lib/cryptsetup/scripts/udp_keyscript
   ```
   Replace the UUID with the one belonging to your encrypted volume (`blkid -s UUID -o value /dev/sda3`).
   You need to regenerate initramfs after changing /etc/crypttab.

5. **Reboot** the machine. During the early‑boot stage the initramfs will:
   - Run the key‑script.
   - Contact the UDP server, obtain the token (retrying up to `$RETRIES` times if needed).
   - Combine it with the static secret, hash it, and feed the result to `cryptsetup`.
   - Unlock the root filesystem automatically.

---

## Files in this directory
| File | Description |
|------|-------------|
| `setup.sh` | Interactive installer that gathers parameters, writes the static secret, generates the key‑script with retry logic, installs the initramfs hook, and updates the initramfs. |
| `udp_keyscript` | Template for the cryptsetup key‑script (placeholders are filled by the installer). Uses `nc`, `sha256sum`, `awk`, and `xxd` with configurable retries. |
| `udp_client_hook` | Initramfs‑tools hook that copies the required utilities (`nc`, `sha256sum`, `awk`, `xxd`) and the generated key‑script into the initramfs image. |
| `README.md` | This documentation (updated for the netcat‑based approach). |

## Adding the fetched key to a LUKS keyslot

The `udp_keyscript` expects the secret file at `/conf/unattended-cryptsetup-keys` inside the initramfs. On the host, the same file is installed at `/etc/initramfs-tools/conf.d/unattended-cryptsetup-keys` and contains both `STATIC_KEY` and `TRIGGER`.

```bash
# Load the static secret and trigger
. /etc/initramfs-tools/conf.d/unattended-cryptsetup-keys

# Run the pipeline and capture the raw 32‑byte key
(echo -n "$TRIGGER" | nc -u -w "$TIMEOUT" "$SERVER_IP" "$SERVER_PORT" \
    | { printf "%s" "$STATIC_KEY"; cat; } \
    | sha256sum | awk '{print $1}' | xxd -r -p) > /tmp/luks_key.bin

# Add the key to a LUKS device (replace /dev/sdX with your encrypted device)
sudo cryptsetup luksAddKey /dev/sdX /tmp/luks_key.bin

# Securely delete the temporary file
shred -u /tmp/luks_key.bin
```

Alternatively, pipe directly without a temporary file:

```bash
. /etc/initramfs-tools/conf.d/unattended-cryptsetup-keys
(echo -n "$TRIGGER" | nc -u -w "$TIMEOUT" "$SERVER_IP" "$SERVER_PORT" \
    | { printf "%s" "$STATIC_KEY"; cat; } \
    | sha256sum | awk '{print $1}' | xxd -r -p) | sudo cryptsetup luksAddKey /dev/sdX -
```

Verify the new keyslot:

```bash
sudo cryptsetup luksDump /dev/sdX
```

**Important:** Keep the secret file (`/etc/initramfs-tools/conf.d/unattended-cryptsetup-keys`) and the UDP server configuration safe, as they are required to regenerate the same key in the future.

---

## Cleanup (optional)
If you ever want to revert the changes:
```bash
# Remove the keyscript
sudo rm /usr/lib/cryptsetup/scripts/udp_keyscript
# Remove the static secret file
sudo rm /etc/initramfs-tools/conf.d/unattended-cryptsetup-keys
# Remove the initramfs hook
sudo rm /etc/initramfs-tools/hooks/udp-client
# Re‑generate initramfs without the hook
sudo update-initramfs -u -k $(uname -r)
```
Then edit `/etc/crypttab` to delete the `keyscript=` entry.

---

**You now have a fully automated, network‑based LUKS unlock ready for testing.**
