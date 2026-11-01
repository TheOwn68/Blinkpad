#!/usr/bin/env bash
# Install ThinkPad Lights (native C) + permissions.
set -euo pipefail

APP_DIR="${HOME}/.local/share/thinkpad-lights"
BIN_DIR="${HOME}/.local/bin"
APPS_DIR="${HOME}/.local/share/applications"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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
install -m 755 "$SRC_DIR/thinkpad-lights" "$SRC_DIR/thinkpad-lights-gui" "$APP_DIR/"
ln -sf "$APP_DIR/thinkpad-lights" "$BIN_DIR/thinkpad-lights"
ln -sf "$APP_DIR/thinkpad-lights-gui" "$BIN_DIR/thinkpad-lights-gui"

cat > "$APPS_DIR/thinkpad-lights.desktop" <<EOF
[Desktop Entry]
Name=ThinkPad Lights
Comment=Control ThinkPad keyboard and status LEDs
Exec=$BIN_DIR/thinkpad-lights-gui
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
    CONF="/tmp/thinkpad-lights-tmpfiles.conf"
    : > "$CONF"
    for led in /sys/class/leds/tpacpi::* /sys/class/leds/*thinkpad* /sys/class/leds/*kbd_backlight* /sys/class/leds/platform::*; do
        [ -e "$led" ] || continue
        for attr in brightness trigger; do
            if [ -e "$led/$attr" ]; then
                echo "z $led/$attr 0664 root $GROUP - -" >> "$CONF"
            fi
        done
    done
    pkexec cp "$CONF" /etc/tmpfiles.d/thinkpad-lights.conf
    pkexec systemd-tmpfiles --create /etc/tmpfiles.d/thinkpad-lights.conf
elif have udevadm; then
    echo "Setting up udev rule permissions..."
    RULE="/tmp/99-thinkpad-lights.rules"
    cat > "$RULE" <<EOF
SUBSYSTEM=="leds", KERNEL=="tpacpi::*", RUN+="/bin/chgrp $GROUP /sys/class/leds/%k/brightness", RUN+="/bin/chmod g+w /sys/class/leds/%k/brightness"
SUBSYSTEM=="leds", KERNEL=="tpacpi::*", RUN+="/bin/chgrp $GROUP /sys/class/leds/%k/trigger", RUN+="/bin/chmod g+w /sys/class/leds/%k/trigger"
SUBSYSTEM=="leds", KERNEL=="platform::*", RUN+="/bin/chgrp $GROUP /sys/class/leds/%k/brightness", RUN+="/bin/chmod g+w /sys/class/leds/%k/brightness"
SUBSYSTEM=="leds", KERNEL=="platform::*", RUN+="/bin/chgrp $GROUP /sys/class/leds/%k/trigger", RUN+="/bin/chmod g+w /sys/class/leds/%k/trigger"
EOF
    pkexec cp "$RULE" /etc/udev/rules.d/99-thinkpad-lights.rules
    pkexec udevadm control --reload-rules || true
    pkexec udevadm trigger --subsystem-match=leds || true
    for led in /sys/class/leds/tpacpi::* /sys/class/leds/platform::*; do
        [ -e "$led" ] || continue
        for attr in brightness trigger; do
            if [ -e "$led/$attr" ]; then
                pkexec chgrp "$GROUP" "$led/$attr" || true
                pkexec chmod g+w "$led/$attr" || true
            fi
        done
    done
else
    echo "No systemd/udev detected. chmod 0666 now (not persistent)."
    for led in /sys/class/leds/tpacpi::* /sys/class/leds/platform::*; do
        [ -e "$led" ] || continue
        for attr in brightness trigger; do
            if [ -e "$led/$attr" ]; then
                pkexec chmod 0666 "$led/$attr" || true
            fi
        done
    done
fi

if have systemctl; then
    mkdir -p "$HOME/.config/systemd/user"
    cat > "$HOME/.config/systemd/user/thinkpad-lights-daemon.service" <<EOF
[Unit]
Description=ThinkPad Lights notification daemon

[Service]
ExecStart=$BIN_DIR/thinkpad-lights daemon
Restart=on-failure

[Install]
WantedBy=default.target
EOF
    echo "Installed user service: thinkpad-lights-daemon.service"
    echo "Enable with: systemctl --user enable --now thinkpad-lights-daemon.service"
fi

echo ""
echo "Done. Ensure $BIN_DIR is in your PATH. Run: thinkpad-lights list"