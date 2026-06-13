#!/usr/bin/env bash
# diagnose.sh — gamepad stack diagnostic
# Walks the full path a controller takes from USB plug to Steam-ready input.
# Use this to find WHERE the chain breaks when a controller shows in lsusb
# but won't work (and only a reboot fixes it).
#
# Layers checked:
#   1. USB        — is the device on the bus?
#   2. Kernel     — is a driver bound? errors in dmesg?
#   3. Input      — does /proc/bus/input/devices list it?
#   4. Device node — does /dev/input/event* / js* exist, readable?
#   5. Permissions — is your user in the right groups?
#   6. udev       — are Steam/controller rules present?
#   7. SDL        — would SDL see it as a gamepad?

set -u
BOLD='\033[1m'; GREEN='\033[1;32m'; RED='\033[1;31m'
YELLOW='\033[1;33m'; CYAN='\033[1;36m'; DIM='\033[2m'; OFF='\033[0m'

layer_status="ok"
layer_names=()
layer_statuses=()

ok()   { printf "  ${GREEN}OK${OFF}      %s\n" "$1"; }
bad()  { layer_status="fail"; printf "  ${RED}FAIL${OFF}    %s\n" "$1"; }
warn() { if [ "$layer_status" = "ok" ]; then layer_status="warn"; fi; printf "  ${YELLOW}WARN${OFF}    %s\n" "$1"; }
info() { printf "  ${CYAN}INFO${OFF}    %s\n" "$1"; }
hdr()  { printf "\n${BOLD}=== %s ===${OFF}\n" "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

reset_layer() { layer_status="ok"; }
record_layer() {
    layer_names+=("$1")
    layer_statuses+=("$layer_status")
}

summary_color() {
    case "$1" in
        ok)    printf '%b' "$GREEN" ;;
        warn)  printf '%b' "$YELLOW" ;;
        fail)  printf '%b' "$RED" ;;
    esac
}

print_summary() {
    hdr "LAYER SUMMARY"
    local name status color label
    for i in "${!layer_names[@]}"; do
        name="${layer_names[$i]}"
        status="${layer_statuses[$i]}"
        color=$(summary_color "$status")
        case "$status" in
            ok)   label="OK" ;;
            warn) label="WARN" ;;
            fail) label="FAIL" ;;
        esac
        printf "  %b%-4s%b  %s\n" "$color" "$label" "$OFF" "$name"
    done
}

hdr "GAMEPAD STACK DIAGNOSTIC"
info "user: $(id -un)   uid: $(id -u)   date: $(date)"

# ---- 0. Helper binaries ----
script_dir="$(dirname "$(readlink -f "$0")")"
hdr "Build helper binaries"
reset_layer
need_build=0
for bin in sdl2-gamepad-test sdl2-gamepad-gui evdev-gamepad-test; do
    if [ ! -x "$script_dir/$bin" ]; then
        need_build=1
        break
    fi
done
if [ "$need_build" -eq 0 ]; then
    ok "helper binaries present"
else
    warn "missing helper binaries; running make..."
    if [ -f "$script_dir/Makefile" ] && (cd "$script_dir" && make all); then
        ok "helper binaries built"
    else
        bad "failed to build helper binaries"
        info "try manually: cd $script_dir && make"
    fi
fi
record_layer "Helper binaries"

# ---- 1. USB layer ----
hdr "1. USB layer (lsusb)"
reset_layer
if have lsusb; then
    usb=$(lsusb)
    echo "$usb" | grep -iE '045e:|054c:|057e:|28de:|146b:|0f0d:|2341:|1532:|20d0:' \
        > /tmp/.gp_usb.txt
    # also match anything with "gamepad|controller|xbox|playstation|dualshock|dualsense|gamepad"
    echo "$usb" | grep -iE 'gamepad|controller|xbox|playstation|dualshock|dualsense|steam input|joy' \
        >> /tmp/.gp_usb.txt
    found=$(sort -u /tmp/.gp_usb.txt)
    if [ -n "$found" ]; then
        while IFS= read -r line; do ok "$line"; done <<< "$found"
    else
        bad "No known gamepad vendor/product in lsusb."
        info "Full lsusb for manual inspection:"
        lsusb | sed 's/^/      /'
    fi
    rm -f /tmp/.gp_usb.txt
else
    bad "lsusb not found (install usbutils)."
fi
record_layer "USB layer"

# ---- 2. Kernel driver / module ----
hdr "2. Kernel driver & modules"
reset_layer
for mod in xpad ff_memless joydev uhid hid_generic usbhid uinput; do
    if lsmod 2>/dev/null | grep -q "^$mod "; then
        ok "module '$mod' loaded"
    else
        warn "module '$mod' NOT loaded"
    fi
done
info "Recent dmesg for gamepad/input (last 15 lines):"
if have dmesg; then
    # needs root for full log on some distros; degrade gracefully
    dmesg 2>/dev/null | grep -iE 'xpad|gamepad|input|hid|controller|xbox' \
        | tail -15 | sed 's/^/      /' || warn "cannot read dmesg (try: sudo $0)"
else
    warn "dmesg not available"
fi
record_layer "Kernel driver & modules"

# ---- 3. /proc/bus/input/devices ----
hdr "3. Kernel input devices (/proc/bus/input/devices)"
reset_layer
if [ -r /proc/bus/input/devices ]; then
    # extract device blocks that look like gamepads
    awk '/^I:/{blk=""} {blk=blk $0 "\n"} /^$|^H:/{ if(blk ~ /Vendor=045e|Vendor=054c|Vendor=057e|Vendor=28de|xpad|gamepad|X-Box|Xbox|DualShock|DualSense|Steam/) printf "%s",blk }' \
        /proc/bus/input/devices > /tmp/.gp_inp.txt
    if [ -s /tmp/.gp_inp.txt ]; then
        ok "Gamepad-like input devices found:"
        sed 's/^/      /' /tmp/.gp_inp.txt
    else
        bad "NO gamepad in /proc/bus/input/devices."
        info "This is the breakpoint: USB sees it but the kernel driver"
        info "has not created an input device. Likely causes:"
        info "  - driver crashed/stuck (xpad known issue after replug)"
        info "  - module not bound: check 'lsusb -t' and dmesg"
        info "  - try: sudo modprobe -r xpad && sudo modprobe xpad"
        info "  - or:  echo '0' | sudo tee /sys/bus/usb/drivers/xpad/unbind  (then rebind)"
    fi
    rm -f /tmp/.gp_inp.txt
else
    bad "/proc/bus/input/devices not readable"
fi
record_layer "Kernel input devices"

# ---- 4. Device nodes ----
hdr "4. Device nodes (/dev/input)"
reset_layer
js_nodes=$(ls /dev/input/js* 2>/dev/null)
if [ -n "$js_nodes" ]; then
    ok "joystick nodes: $js_nodes"
else
    warn "no /dev/input/js* nodes (joydev may be missing or no gamepad bound)"
fi
ev_nodes=$(ls /dev/input/event* 2>/dev/null | head -30)
if [ -n "$ev_nodes" ]; then
    info "event nodes exist ($(echo "$ev_nodes" | wc -l) total)"
else
    bad "no /dev/input/event* nodes at all (very wrong)"
fi
record_layer "Device nodes"

# ---- 5. Permissions ----
hdr "5. Permissions & groups"
reset_layer
groups=$(id -nG 2>/dev/null)
if echo "$groups" | grep -qw input; then
    ok "user is in 'input' group (can read /dev/input/event*)"
else
    warn "user NOT in 'input' group"
    info "  Many event nodes are root:input 0660 — you may get EACCES."
    info "  Fix: sudo usermod -aG input $USER  (then re-login)"
    info "  OR rely on uaccess (udev TAG) for the specific gamepad node."
fi
if echo "$groups" | grep -qw uinput; then
    ok "user is in 'uinput' group (for virtual/Steam controllers)"
else
    warn "user NOT in 'uinput' group (Steam virtual devices may fail)"
fi
info "sample node ownership:"
ls -l /dev/input/js0 2>/dev/null | sed 's/^/      /'
ls -l /dev/input/event7 2>/dev/null | sed 's/^/      /'
record_layer "Permissions & groups"

# ---- 6. udev rules ----
hdr "6. udev rules (Steam / controller)"
reset_layer
for r in /lib/udev/rules.d/60-steam-input.rules \
         /lib/udev/rules.d/70-joystick.rules \
         /etc/udev/rules.d/99-controller.rules; do
    if [ -f "$r" ]; then ok "present: $r"; else warn "missing: $r"; fi
done
info "To reload udev after adding rules:"
info "  sudo udevadm control --reload-rules && sudo udevadm trigger"
record_layer "udev rules"

# ---- 7. SDL visibility ----
hdr "7. SDL visibility"
reset_layer
sdlbin="$script_dir/sdl2-gamepad-test"
if [ -x "$sdlbin" ]; then
    info "running sdl2-gamepad-test..."
    info "Some controllers only wake up when you move a stick or press a button."
    info "Use the controller, then press Enter in the SDL window to finish."
    "$sdlbin" >/tmp/.gp_sdl.txt 2>&1 & pid=$!
    wait "$pid" 2>/dev/null
    if grep -qE 'Joysticks found: [1-9]|\[BTN DOWN\]|\[BTN UP\]|\[AXIS\]|Listening on:' /tmp/.gp_sdl.txt; then
        ok "SDL sees joystick(s):"
        grep -E 'Name:|GUID:|Gamepad:|Listening on:' /tmp/.gp_sdl.txt | sed 's/^/      /'
    else
        bad "SDL sees 0 joysticks (but kernel may see it -> SDL mapping/driver issue)"
    fi
    rm -f /tmp/.gp_sdl.txt
else
    warn "sdl2-gamepad-test still missing after build; skipping SDL probe"
fi
record_layer "SDL visibility"

# ---- summary ----
hdr "QUICK FIXES (if broken at a layer)"
cat <<'TIPS'
  Layer 2/3 broken (USB ok, no input device):
    sudo modprobe -r xpad && sudo modprobe xpad        # reload driver
    # OR rebind the USB device without reboot:
    ls /sys/bus/usb/drivers/xpad/                       # find the device id
    echo '<id>' | sudo tee /sys/bus/usb/drivers/xpad/unbind
    echo '<id>' | sudo tee /sys/bus/usb/drivers/xpad/bind
    # OR full USB reset:
    sudo udevadm trigger --action=add --subsystem-match=input

  Layer 4/5 broken (input device exists, can't read):
    sudo usermod -aG input $USER && sudo usermod -aG uinput $USER
    # log out and back in for group change to take effect

  Layer 7 broken (kernel sees it, SDL/Steam doesn't):
    - missing controller mapping (gamecontrollerdb.txt)
    - Steam may be grabbing it exclusively: disable Steam Input for the game
TIPS
print_summary
printf "\n${DIM}done.${OFF}\n"
