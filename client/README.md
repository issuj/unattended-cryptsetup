# README – Static UDP client in the Ubuntu 24.04 initramfs  

This document explains how to **build the UDP client with musl** (fully static) and
make it available inside the initramfs that the kernel boots from.  
All steps are tested on Ubuntu 24.04.

---  

## 1. What you will get  

* `/usr/bin/udp-client` – a single ELF binary (≈ 30 KB) that can be executed
  from the initramfs shell.
* No extra shared libraries are required (musl contains its own resolver).

---  

## 2. Prerequisites  

```bash
sudo apt update
sudo apt install -y musl-tools initramfs-tools   # (or dracut if you use dracut)
```

* `musl-tools` → provides `musl-gcc` for static linking.  
* `initramfs-tools` → the default initramfs generator on Ubuntu.

---  

## 3. Build the static UDP client  

```bash
# 1️⃣  Download / create the source file (udp-client.c)
#    (Copy the source from the previous answer into this file.)

# 2️⃣  Compile a **musl‑static** binary
musl-gcc -static -Wall -Wextra -Os -s udp-client.c -o udp-client

# 3️⃣  Verify that it is truly static
ldd ./udp-client          # should print: "not a dynamic executable"
file ./udp-client         # should contain "statically linked"
```

If the checks succeed you have a portable binary that works without any
runtime libraries.

---  

## 4. Install the binary where the initramfs hook can find it  

```bash
sudo install -m 0755 udp-client /usr/local/sbin/udp-client
```

(Any location works – just keep the path consistent with the hook script.)

---  

## 5. Create an **initramfs‑tools** hook  

`initramfs-tools` runs every executable it finds in
`/etc/initramfs-tools/hooks/` while building the initramfs.  
Create the following file (root‑owned, executable):

```bash
sudo tee /etc/initramfs-tools/hooks/udp-client > /dev/null <<'EOF'
#!/bin/sh
# initramfs‑tools hook – copy the musl‑static UDP client into the initramfs

PREREQ=""

prereqs() {
    echo "$PREREQ"
}
case "$1" in
    prereqs) prereqs; exit 0 ;;
esac

. /usr/share/initramfs-tools/hook-functions

# -----------------------------------------------------------------
# Copy the binary to /usr/bin inside the initramfs (you may change the
# destination if you prefer another location).
# -----------------------------------------------------------------
copy_exec /usr/local/sbin/udp-client /usr/bin/udp-client

exit 0
EOF
sudo chmod +x /etc/initramfs-tools/hooks/udp-client
```

*The hook does **nothing else** because the musl binary already contains the
resolver – no NSS libraries are needed.*

---  

## 6. Re‑build the initramfs  

Re‑create the initramfs for the kernel you are currently running (the one that
will be used on the next boot):

```bash
sudo update-initramfs -u -k $(uname -r)
```

You will see the hook being executed and the file `usr/bin/udp-client` added to
the new archive.

---  

## 7. Test after reboot  

1. Reboot the machine.  
2. When the **initramfs** prompt appears (usually `initramfs>` or a tiny
   busybox shell), run:

```sh
/usr/bin/udp-client <SERVER_HOST> 51818 foo 1 5000
```

* Expected output: the exact reply that the remote UDP service sends.  
* Verify the exit status:

```sh
echo $?
# should print 0
```

If you see the reply and the status is 0, the client is correctly installed
and usable from the initramfs.

---  

## 8. (Optional) Re‑build a custom initramfs for testing only  

If you do **not** want to replace the system’s current initramfs, you can
create a temporary one:

```bash
TMPDIR=$(mktemp -d)
cp /boot/initrd.img-$(uname -r) "$TMPDIR/initrd.img"

cd "$TMPDIR"
mkdir unpack
cd unpack
gzip -dc ../initrd.img | cpio -idmv          # unpack
sudo /etc/initramfs-tools/hooks/udp-client   # run the hook manually
find . | cpio --quiet -o -H newc | gzip -9 > ../test-initrd.img
cd ..
ls -lh test-initrd.img
```

Boot the machine with `test-initrd.img` by editing the GRUB entry
(`initrd /path/to/test-initrd.img`) and you can test without touching the
distribution’s default initramfs.

---  

## 9. Summary of commands (copy‑paste)

```bash
# ----- 1. prerequisites -------------------------------------------------
sudo apt update
sudo apt install -y musl-tools initramfs-tools

# ----- 2. compile -------------------------------------------------------
musl-gcc -static -Wall -Wextra -Os -s udp-client.c -o udp-client
ldd ./udp-client          # → "not a dynamic executable"
file ./udp-client         # → "... statically linked ..."

# ----- 3. install binary ------------------------------------------------
sudo install -m 0755 udp-client /usr/local/sbin/udp-client

# ----- 4. create initramfs hook -----------------------------------------
sudo tee /etc/initramfs-tools/hooks/udp-client > /dev/null <<'EOF'
#!/bin/sh
PREREQ=""; prereqs(){ echo "$PREREQ"; }
case "$1" in prereqs) prereqs; exit 0;; esac
. /usr/share/initramfs-tools/hook-functions
copy_exec /usr/local/sbin/udp-client /usr/bin/udp-client
exit 0
EOF
sudo chmod +x /etc/initramfs-tools/hooks/udp-client

# ----- 5. rebuild initramfs ---------------------------------------------
sudo update-initramfs -u -k $(uname -r)

# ----- 6. reboot --------------------------------------------------------
sudo reboot
# (after boot, at initramfs prompt:)
# /usr/bin/udp-client <SERVER_HOST> 51818 foo 1 5000
```

That’s it – you now have a **self‑contained UDP client** available as soon as
the kernel hands control to the initramfs. Happy hacking!
