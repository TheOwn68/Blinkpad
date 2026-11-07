# Blinkpad v1.3.1 (native C)

For colorless (monochrome) ThinkPad LEDs.

Control ThinkPad keyboard backlight and status LEDs (power, standby, ThinkVantage, lid logo dot)
from Linux, with software patterns and a notification flash.

## Features
- Native C binaries: `blinkpad` (CLI) and `blinkpad-gui` (GTK3 GUI).
- Modern dark interface with a card layout, per-LED icons, and wrapping LED chips.
- Built-in **Commands** tab documenting every terminal command, with one-click copy buttons.
- Per-LED brightness and kernel trigger selection.
- Patterns: solid, blink, strobe, pulse, heartbeat, chase, cylon, wave, SOS, notification flash.
- Trigger tools: `blinkpad triggers [led]` lists supported kernel triggers, and `blinkpad trigger <led> <name>` accepts short aliases (`caps`, `num`, `scroll`, `mute`, `mic`, `battery`, …).
- Notification daemon: flashes a chosen LED when a desktop notification arrives.
- System tray: closing or minimizing while a pattern or the notification daemon is running keeps Blinkpad alive in the tray (StatusNotifierItem — works on KDE Plasma, including Wayland).
- Portable across distros: discovers LEDs at runtime; sets up permissions via systemd-tmpfiles, udev, or chmod fallback. Permissions now also cover `input*::` LEDs (Caps/Num/Scroll Lock) so they keep working after a reboot on Fedora.

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
sudo dnf install gcc make gtk3-devel pkgconf
```

## Install
```
cd blinkpad
./install.sh
```
`install.sh` builds, installs to `~/.local/share/blinkpad`, symlinks into `~/.local/bin`, adds a desktop entry, and fixes LED file permissions (may prompt via polkit).

If `blinkpad` is not found after install, ensure `~/.local/bin` is in your PATH (log out/in or add it to your shell profile).

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
blinkpad triggers
blinkpad trigger capslock caps
```
GUI: `blinkpad-gui`

## Permissions
LED sysfs files are root-owned. `install.sh` makes them group-writable for your user via the best available mechanism. You can re-run `blinkpad ensure-permissions`.

## Notes
- Keyboard backlight usually supports only 0-2 levels.
- Patterns are software-driven.

## Mute LED note

The speaker/mic mute LEDs are normally driven by the audio driver (`audio-mute` / `audio-micmute` triggers). Blinkpad avoids touching them in default actions (All Off/All Max/patterns), but if you manually change their trigger or brightness you may desync them from the actual mute state. To restore: set trigger back to `audio-mute` / `audio-micmute` in the Lights tab, or run `blinkpad trigger mute mute` / `blinkpad trigger micmute mic`.

An active trigger only makes the LED *follow* the system state; Blinkpad never changes the audio or lock state itself. Choose the `none` trigger for manual control.

List the available triggers and their shortcuts with `blinkpad triggers` and `blinkpad triggers <led>`.

## Troubleshooting

**Fedora: permission prompt fails / LEDs still not writable**
- Run the installer as root: `sudo ./install.sh`
- Or manually: `sudo blinkpad ensure-permissions`
- Ensure polkit is installed: `sudo dnf install polkit`

**LEDs not discovered**
- Ensure `thinkpad_acpi` is loaded: `lsmod | grep thinkpad`
- Some models need `brightness_enable=1`: `echo 'options thinkpad_acpi brightness_enable=1' | sudo tee /etc/modprobe.d/thinkpad.conf`