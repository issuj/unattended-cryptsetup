# Tools to let a Linux with encrypted root to boot unattended (fetching part of the secret over network)

Made by AI: Mistral Vibe with local GPT-OSS-120b

In the end this required quite a bit of human intervention to get working. The current version actually works as long as you provide your initramfs with network connectivity before udp_keyscript runs. That may include adding ip=... kernel parameter, and making sure it's applied before needing it.

My threat model: I have a server in a relatively insecure location, someone could break a window to gain access, grab the hardware and run with it. They're unlikely to hang around on site to investigate the boot sequence of that server, or what's available on the network the server is connected to.

If your threat model doesn't match mine, this will be a stupid idea.
