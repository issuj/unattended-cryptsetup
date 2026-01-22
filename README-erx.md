# EdgeRouter UDP Responder Service Setup

This guide explains how to run `udp_responder.py` as a persistent service on EdgeRouter X that survives both reboots and firmware updates.

## Prerequisites

- Script location: `/config/scripts/udp_responder.py`
- Pair file location: `/home/USER/secret.file`
- Python interpreter: `/usr/bin/python`
- Service runs as user: `USER`

## Installation

### 1. Create the post-config startup script

```bash
sudo vi /config/scripts/post-config.d/start-udp-responder.sh
```

Add the following content:

```bash
#!/bin/bash

# Create systemd service file if it doesn't exist
SERVICE_FILE="/etc/systemd/system/udp-responder.service"

if [ ! -f "$SERVICE_FILE" ]; then
    cat > "$SERVICE_FILE" << 'EOF'
[Unit]
Description=UDP Responder Service
After=network.target

[Service]
Type=simple
User=USER
WorkingDirectory=/config/scripts
ExecStart=/usr/bin/python /config/scripts/udp_responder.py --pair-file /home/USER/secret.file
Restart=always
RestartSec=10
StandardOutput=journal
StandardError=journal
SyslogIdentifier=udp-responder

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable udp-responder.service
fi

# Ensure service is running
systemctl start udp-responder.service
```

### 2. Make the script executable

```bash
sudo chmod +x /config/scripts/post-config.d/start-udp-responder.sh
```

### 3. Run the script to start the service

```bash
sudo /config/scripts/post-config.d/start-udp-responder.sh
```

### 4. Verify the service is running

```bash
sudo systemctl status udp-responder.service
```

You should see `Active: active (running)` in the output.

## Managing the Service

### Check service status
```bash
sudo systemctl status udp-responder.service
```

### Stop the service
```bash
sudo systemctl stop udp-responder.service
```

### Start the service
```bash
sudo systemctl start udp-responder.service
```

### Restart the service
```bash
sudo systemctl restart udp-responder.service
```

## Viewing Logs

### Follow logs in real-time
```bash
sudo journalctl -u udp-responder.service -f
```

### View recent logs (last 100 lines)
```bash
sudo journalctl -u udp-responder.service -n 100
```

### View logs from a specific time
```bash
sudo journalctl -u udp-responder.service --since "1 hour ago"
sudo journalctl -u udp-responder.service --since "2026-01-17 00:00:00"
```

### View logs with timestamps
```bash
sudo journalctl -u udp-responder.service -o short-iso
```

## Persistence

This setup ensures the service:
- ✓ Starts automatically on boot
- ✓ Restarts automatically if it crashes
- ✓ Survives firmware updates (because files are in `/config`)
- ✓ Logs are captured via systemd journal

## Troubleshooting

### Service fails to start

Check the logs for error messages:
```bash
sudo journalctl -u udp-responder.service -n 50
```

### Service not running after reboot

Verify the post-config script is executable:
```bash
ls -l /config/scripts/post-config.d/start-udp-responder.sh
```

Run it manually to see any errors:
```bash
sudo /config/scripts/post-config.d/start-udp-responder.sh
```

### After firmware update

The service should automatically recreate itself. If not, run the post-config script manually:
```bash
sudo /config/scripts/post-config.d/start-udp-responder.sh
```

## Uninstallation

To remove the service:

```bash
sudo systemctl stop udp-responder.service
sudo systemctl disable udp-responder.service
sudo rm /etc/systemd/system/udp-responder.service
sudo rm /config/scripts/post-config.d/start-udp-responder.sh
sudo systemctl daemon-reload
```
