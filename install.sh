#!/usr/bin/env bash
# Install Blinkpad (native C) + permissions.
set -euo pipefail

# Run a command as root (run_root when unprivileged, directly when already root)
run_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        pkexec "$@"
    fi
}

APP_DIR="${HOME}/.local/share/blinkpad"
BIN_DIR="${HOME}/.local/bin"
APPS_DIR="${HOME}/.local/share/applications"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Dependency checks
missing=0
for cmd in gcc make pkg-config; do
    if ! command -v "$cmd" >/dev/null 2>&1; then echo "Missing: $cmd"; missing=1; fi
done
if ! pkg-config --exists gtk+-3.0 2>/dev/null; then echo "Missing: GTK3 development files (pkg-config gtk+-3.0)"; missing=1; fi
if [ "$missing" -eq 1 ]; then
    echo ""
    echo "Install build deps:"
    echo "  Arch:   sudo pacman -S base-devel gtk3"
    echo "  Debian: sudo apt install build-essential libgtk-3-dev"
    echo "  Fedora: sudo dnf install gcc make gtk3-devel pkgconf"
    exit 1
fi

echo "Source: $SRC_DIR"

# Check build deps
if ! command -v gcc >/dev/null 2>&1; then echo "gcc not found"; exit 1; fi
if ! pkg-config --exists gtk+-3.0 2>/dev/null; then
    echo "gtk+-3.0 dev files not found. Install python-gobject? no—install GTK3 dev:"
    echo "  Arch: sudo pacman -S gtk3"
    echo "  Debian: sudo apt install libgtk-3-dev"
    echo "  Fedora: sudo dnf install gtk3-devel"
    exit 1
fi

cd "$SRC_DIR"
make clean >/dev/null 2>&1 || true
make

mkdir -p "$APP_DIR" "$BIN_DIR" "$APPS_DIR"
install -m 755 "$SRC_DIR/blinkpad" "$SRC_DIR/blinkpad-gui" "$APP_DIR/"
ln -sf "$APP_DIR/blinkpad" "$BIN_DIR/blinkpad"
ln -sf "$APP_DIR/blinkpad-gui" "$BIN_DIR/blinkpad-gui"

cat > "$APPS_DIR/blinkpad.desktop" <<EOF
[Desktop Entry]
Name=Blinkpad
Comment=Control ThinkPad keyboard and status LEDs
Exec=$BIN_DIR/blinkpad-gui
Type=Application
Terminal=false
Categories=Utility;HardwareSettings;
Icon=input-keyboard
EOF
update-desktop-database "$APPS_DIR" 2>/dev/null || true

echo "Installed to $APP_DIR"

GROUP="$(id -gn)"
echo "User group: $GROUP"

have() { command -v "$1" >/dev/null 2>&1; }

# Make every LED attribute writable by the user's group, persistently.
# This must include input*::capslock/numlock/scrolllock, not just the
# ThinkPad-specific tpacpi:: and platform:: lights: input LEDs are
# recreated with root-only permissions on every boot on Fedora.
LED_GLOB="/sys/class/leds/*"

if have udevadm; then
    echo "Setting up udev rule permissions..."
    RULE="/tmp/99-blinkpad.rules"
    cat > "$RULE" <<EOF
SUBSYSTEM=="leds", ACTION=="add|change", RUN+="/bin/sh -c 'chgrp $GROUP /sys/class/leds/%k/brightness /sys/class/leds/%k/trigger 2>/dev/null; chmod g+w /sys/class/leds/%k/brightness /sys/class/leds/%k/trigger 2>/dev/null'"
EOF
    run_root cp "$RULE" /etc/udev/rules.d/99-blinkpad.rules
    run_root udevadm control --reload-rules || true
    run_root udevadm trigger --subsystem-match=leds || true
fi

if have systemctl && have systemd-tmpfiles; then
    echo "Setting up systemd-tmpfiles permissions..."
    CONF="/tmp/blinkpad-tmpfiles.conf"
    : > "$CONF"
    for led in $LED_GLOB; do
        [ -e "$led" ] || continue
        for attr in brightness trigger; do
            if [ -e "$led/$attr" ]; then
                echo "z $led/$attr 0664 root $GROUP - -" >> "$CONF"
            fi
        done
    done
    run_root cp "$CONF" /etc/tmpfiles.d/blinkpad.conf
    run_root systemd-tmpfiles --create /etc/tmpfiles.d/blinkpad.conf
fi

# Apply immediately too, in one privileged command.
ATTRS=""
for led in $LED_GLOB; do
    [ -e "$led" ] || continue
    for attr in brightness trigger; do
        [ -e "$led/$attr" ] && ATTRS="$ATTRS $led/$attr"
    done
done
if [ -n "$ATTRS" ]; then
    run_root sh -c "chgrp '$GROUP' $ATTRS 2>/dev/null; chmod g+w $ATTRS 2>/dev/null" || true
fi

if have systemctl; then
    mkdir -p "$HOME/.config/systemd/user"
    cat > "$HOME/.config/systemd/user/blinkpad-daemon.service" <<EOF
[Unit]
Description=Blinkpad notification daemon

[Service]
ExecStart=$BIN_DIR/blinkpad daemon
Restart=on-failure

[Install]
WantedBy=default.target
EOF
    echo "Installed user service: blinkpad-daemon.service"
    echo "Enable with: systemctl --user enable --now blinkpad-daemon.service"
fi

# Sleep hook: restore keyboard backlight to manual control after wake
HOOK_DIR="/etc/systemd/system-sleep"
[ -d "$HOOK_DIR" ] || HOOK_DIR="/usr/lib/systemd/system-sleep"
run_root mkdir -p "$HOOK_DIR" 2>/dev/null || true
HOOK="$HOOK_DIR/blinkpad-kbd.sh"
TMPHOOK="/tmp/blinkpad-kbd.sh"
cat > "$TMPHOOK" <<'HOOKEOF'
#!/bin/sh
if [ "$1" = "post" ]; then
    echo none > /sys/class/leds/tpacpi::kbd_backlight/trigger 2>/dev/null
    echo 0 > /sys/class/leds/tpacpi::kbd_backlight/brightness 2>/dev/null
fi
HOOKEOF
run_root cp "$TMPHOOK" "$HOOK"
run_root chmod +x "$HOOK"

echo ""
echo "Done. Ensure $BIN_DIR is in your PATH. Run: blinkpad list"