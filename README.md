# Tools to let a Linux with encrypted root to boot unattended (fetching part of the secret over network)

Made by AI: Mistral Vibe with local GPT-OSS-120b

In the end this required quite a bit of human intervention to get working. The current version actually works as long as you provide your initramfs with network connectivity before udp_keyscript runs. That may include adding ip=... kernel parameter, and making sure it's applied before needing it.

Note that if your threat model doesn't match mine, this will be a stupid idea. Use the source (or ask an LLM).
