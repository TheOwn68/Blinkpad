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

if have systemctl && have systemd-tmpfiles; then
    echo "Setting up systemd-tmpfiles permissions..."
    CONF="/tmp/blinkpad-tmpfiles.conf"
    : > "$CONF"
    for led in /sys/class/leds/tpacpi::* /sys/class/leds/*thinkpad* /sys/class/leds/*kbd_backlight* /sys/class/leds/platform::*; do
        [ -e "$led" ] || continue
        for attr in brightness trigger; do
            if [ -e "$led/$attr" ]; then
                echo "z $led/$attr 0664 root $GROUP - -" >> "$CONF"
            fi
        done
    done
    run_root cp "$CONF" /etc/tmpfiles.d/blinkpad.conf
    run_root systemd-tmpfiles --create /etc/tmpfiles.d/blinkpad.conf
elif have udevadm; then
    echo "Setting up udev rule permissions..."
    RULE="/tmp/99-blinkpad.rules"
    cat > "$RULE" <<EOF
SUBSYSTEM=="leds", KERNEL=="tpacpi::*", RUN+="/bin/chgrp $GROUP /sys/class/leds/%k/brightness", RUN+="/bin/chmod g+w /sys/class/leds/%k/brightness"
SUBSYSTEM=="leds", KERNEL=="tpacpi::*", RUN+="/bin/chgrp $GROUP /sys/class/leds/%k/trigger", RUN+="/bin/chmod g+w /sys/class/leds/%k/trigger"
SUBSYSTEM=="leds", KERNEL=="platform::*", RUN+="/bin/chgrp $GROUP /sys/class/leds/%k/brightness", RUN+="/bin/chmod g+w /sys/class/leds/%k/brightness"
SUBSYSTEM=="leds", KERNEL=="platform::*", RUN+="/bin/chgrp $GROUP /sys/class/leds/%k/trigger", RUN+="/bin/chmod g+w /sys/class/leds/%k/trigger"
EOF
    run_root cp "$RULE" /etc/udev/rules.d/99-blinkpad.rules
    run_root udevadm control --reload-rules || true
    run_root udevadm trigger --subsystem-match=leds || true
    for led in /sys/class/leds/tpacpi::* /sys/class/leds/platform::*; do
        [ -e "$led" ] || continue
        for attr in brightness trigger; do
            if [ -e "$led/$attr" ]; then
                run_root chgrp "$GROUP" "$led/$attr" || true
                run_root chmod g+w "$led/$attr" || true
            fi
        done
    done
else
    echo "No systemd/udev detected. chmod 0666 now (not persistent)."
    for led in /sys/class/leds/tpacpi::* /sys/class/leds/platform::*; do
        [ -e "$led" ] || continue
        for attr in brightness trigger; do
            if [ -e "$led/$attr" ]; then
                run_root chmod 0666 "$led/$attr" || true
            fi
        done
    done
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

echo ""
echo "Done. Ensure $BIN_DIR is in your PATH. Run: blinkpad list"