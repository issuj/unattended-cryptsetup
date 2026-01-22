# UDP‑Based LUKS Unlock Setup

## Summary

This directory contains a **self‑contained automation** that lets an Ubuntu 24.04 machine unlock its LUKS‑encrypted root volume **without any manual password entry**. The unlock works by:

1. **Fetching a one‑time token** from a UDP server you run on an EdgeRouter (or any reachable host).
2. **Mixing that token** with a **static secret** that is baked into the initramfs image.
3. **Hashing the combination** with SHA‑256 to produce the 256‑bit key that `cryptsetup` uses to open the LUKS container.

All the heavy lifting happens **inside the initramfs** before the root filesystem is mounted, so the machine can boot unattended while still keeping the disk encrypted.

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

3. **What the installer does**
   - Stores the static secret in `/etc/initramfs-tools/conf.d/static_key` (will be packed into the initramfs).
   - Generates a **key‑script** (`/usr/lib/cryptsetup/scripts/udp_keyscript`) with the values you supplied.
   - Builds a **musl‑static UDP client** (`udp-client.musl`).
   - Installs the binary and a **initramfs‑tools hook** that copies both the binary and the key‑script into the initramfs image.
   - Regenerates the initramfs for the currently running kernel.
   - Prints the line you need to add to `/etc/crypttab`.

4. **Add the key‑script entry to `/etc/crypttab`** (example output from the installer):
   ```text
   cryptroot UUID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx none luks,keyscript=/usr/lib/cryptsetup/scripts/udp_keyscript
   ```
   Replace the UUID with the one belonging to your encrypted volume (`blkid -s UUID -o value /dev/sda3`).

5. **Reboot** the machine. During the early‑boot stage the initramfs will:
   - Run the key‑script.
   - Contact the UDP server, obtain the token.
   - Combine it with the static secret, hash it, and feed the result to `cryptsetup`.
   - Unlock the root filesystem automatically.

6. **Testing & troubleshooting**
   - Verify the UDP server is reachable and returns the expected token.
   - If the boot hangs, switch to a console (Ctrl+Alt+F2) and inspect the initramfs logs (`cat /run/initramfs/debug`).
   - You can always fall back to a password‑based keyslot (add it before removing the password keyslot). 

---

## Files in this directory
| File | Description |
|------|-------------|
| `build_udp_client.sh` | Compiles `udp-client.c` into a fully static binary (`udp-client.musl`). |
| `udp_keyscript` | Template for the cryptsetup key‑script (placeholders are filled by the installer). |
| `udp_client_hook` | Initramfs‑tools hook that copies the static client binary and the generated key‑script into the initramfs image. |
| `setup.sh` | Interactive installer that gathers parameters, writes the static secret, generates the key‑script, builds the client, installs everything, and updates the initramfs. |
| `README.md` | This documentation. |

---

## Cleanup (optional)
If you ever want to revert the changes:
```bash
# Remove the keyscript
sudo rm /usr/lib/cryptsetup/scripts/udp_keyscript
# Remove the static secret file
sudo rm /etc/initramfs-tools/conf.d/static_key
# Remove the initramfs hook
sudo rm /etc/initramfs-tools/hooks/udp-client
# Re‑generate initramfs without the hook
sudo update-initramfs -u -k $(uname -r)
```
Then edit `/etc/crypttab` to delete the `keyscript=` entry.

---

**You now have a fully automated, network‑based LUKS unlock ready for testing.**
