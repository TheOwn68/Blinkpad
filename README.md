# Blinkpad (native C)

Control ThinkPad keyboard backlight and status LEDs (power, standby, ThinkVantage, lid logo dot)
from Linux, with software patterns and a notification flash.

## Features
- Native C binaries: `blinkpad` (CLI) and `blinkpad-gui` (GTK3 GUI).
- Per-LED brightness and kernel trigger selection.
- Patterns: solid, blink, strobe, pulse, heartbeat, chase, cylon, wave, SOS, rainbow, notification flash.
- Notification daemon: flashes a chosen LED when a desktop notification arrives.
- Portable across distros: discovers LEDs at runtime; sets up permissions via systemd-tmpfiles, udev, or chmod fallback.

## Requirements (build)
- gcc, make, pkg-config
- GTK 3 dev files (`gtk+-3.0`)
- GLib/GIO (usually with GTK)

### Arch Linux
```
sudo pacman -S base-devel gtk3
```
### Debian/Ubuntu
```
sudo apt install build-essential libgtk-3-dev
```
### Fedora
```
sudo dnf install gcc make gtk3-devel
```

## Install
```
cd blinkpad
./install.sh
```
`install.sh` builds, installs to `~/.local/share/blinkpad`, symlinks into `~/.local/bin`, adds a desktop entry, and fixes LED file permissions (may prompt via polkit).

Enable notification daemon:
```
systemctl --user enable --now blinkpad-daemon.service
```

## Usage
```
blinkpad list
blinkpad set kbd_backlight 2
blinkpad pattern pulse --leds power,standby --freq 0.5
blinkpad flash --leds thinkvantage --count 5
blinkpad daemon --led thinkvantage
```
GUI: `blinkpad-gui`

## Permissions
LED sysfs files are root-owned. `install.sh` makes them group-writable for your user via the best available mechanism. You can re-run `blinkpad ensure-permissions`.

## Notes
- Keyboard backlight usually supports only 0-2 levels.
- Patterns are software-driven.