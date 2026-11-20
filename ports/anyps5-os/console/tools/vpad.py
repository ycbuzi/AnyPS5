#!/usr/bin/env python3
import argparse
import fcntl
import os
import struct
import sys
import time

USAGE = """Virtual controller on /dev/uinput for hot-plug and button-glyph tests without the real thing.

vpad.py [--kind dualsense|xbox|xbox360|switch|generic] [--usb] [--delay S] [--hold S] [STEP ...]
STEP: button name (cross circle square triangle l1 r1 l2 r2 create options ps l3 r3 up down left right),
      name:MS to hold it MS milliseconds, a+b[:MS] to hold two buttons together, or wait:MS.
      Steps run after --delay, the pad stays --hold seconds.
Buttons are named by position, so cross is A on an Xbox pad and B on a Switch pad; create is View/Select/Minus,
options is Menu/Start/Plus, ps is the Xbox/Home button. The generic pad has no home button: ps presses Start+Select.
Kinds: dualsense (054c:0ce6, Bluetooth identity as hid-playstation reports it, --usb for the cable one),
       xbox (045e:0b13 Xbox Series pad over Bluetooth, stock-kernel layout), xbox360 (045e:028e, xpad layout),
       switch (057e:2009 Pro Controller, hid-nintendo layout), generic (1234:5678, 12 plain joystick buttons, no known layout)."""
UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_SET_ABSBIT = 0x40045567
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_GAS, ABS_BRAKE, ABS_HAT0X, ABS_HAT0Y = 0, 1, 2, 3, 4, 5, 9, 10, 0x10, 0x11
HAT = {"left": (ABS_HAT0X, -1), "right": (ABS_HAT0X, 1), "up": (ABS_HAT0Y, -1), "down": (ABS_HAT0Y, 1)}
GAMEPAD = {
    "cross": 0x130, "circle": 0x131, "triangle": 0x133, "square": 0x134,
    "l1": 0x136, "r1": 0x137, "l2": 0x138, "r2": 0x139,
    "create": 0x13a, "options": 0x13b, "ps": 0x13c, "l3": 0x13d, "r3": 0x13e,
}
XPAD = {
    "cross": 0x130, "circle": 0x131, "square": 0x133, "triangle": 0x134,
    "l1": 0x136, "r1": 0x137, "create": 0x13a, "options": 0x13b, "ps": 0x13c, "l3": 0x13d, "r3": 0x13e,
}
XBOX_BT = dict(XPAD)
JOYSTICK = {
    "triangle": 0x120, "circle": 0x121, "cross": 0x122, "square": 0x123, "l1": 0x124, "r1": 0x125, "l2": 0x126, "r2": 0x127,
    "create": 0x128, "options": 0x129, "l3": 0x12a, "r3": 0x12b,
}
KINDS = {
    "dualsense": {
        "name": "DualSense Wireless Controller", "usb_name": "Sony Interactive Entertainment DualSense Wireless Controller",
        "ids": (0x05, 0x054C, 0x0CE6, 0x8100), "usb_ids": (0x03, 0x054C, 0x0CE6, 0x8111),
        "buttons": GAMEPAD, "sticks": ((ABS_X, ABS_Y, ABS_RX, ABS_RY), 0, 255), "triggers": {"l2": ABS_Z, "r2": ABS_RZ}, "trigger_max": 255,
    },
    "xbox": {
        "name": "Xbox Wireless Controller", "ids": (0x05, 0x045E, 0x0B13, 0x0509),
        "buttons": XBOX_BT, "extra_keys": (0x132, 0x135, 0x138, 0x139),
        "sticks": ((ABS_X, ABS_Y, ABS_Z, ABS_RZ), 0, 65535), "triggers": {"l2": ABS_BRAKE, "r2": ABS_GAS}, "trigger_max": 1023,
    },
    "xbox360": {
        "name": "Microsoft X-Box 360 pad", "ids": (0x03, 0x045E, 0x028E, 0x0114),
        "buttons": XPAD, "sticks": ((ABS_X, ABS_Y, ABS_RX, ABS_RY), -32768, 32767), "triggers": {"l2": ABS_Z, "r2": ABS_RZ}, "trigger_max": 255,
    },
    "switch": {
        "name": "Pro Controller", "ids": (0x05, 0x057E, 0x2009, 0x8001),
        "buttons": GAMEPAD, "extra_keys": (0x135,), "sticks": ((ABS_X, ABS_Y, ABS_RX, ABS_RY), -32767, 32767), "triggers": {}, "trigger_max": 0,
    },
    "generic": {
        "name": "Generic USB Gamepad", "ids": (0x03, 0x1234, 0x5678, 0x0110),
        "buttons": JOYSTICK, "sticks": ((ABS_X, ABS_Y, ABS_Z, ABS_RZ), 0, 255), "triggers": {}, "trigger_max": 0,
        "chords": {"ps": ("options", "create")},
    },
}
STEP_NAMES = sorted(set(GAMEPAD) | set(HAT))


def emit(fd, etype, code, value):
    now = time.time()
    os.write(fd, struct.pack("qqHHi", int(now), int((now % 1) * 1e6), etype, code, value))


def sync(fd):
    emit(fd, EV_SYN, 0, 0)


def expand(kind, names):
    out = []
    for name in names:
        out.extend(kind.get("chords", {}).get(name, (name,)))
    return out


def press(fd, kind, name, down):
    if name in HAT:
        axis, value = HAT[name]
        emit(fd, EV_ABS, axis, value if down else 0)
    else:
        if name in kind["buttons"]:
            emit(fd, EV_KEY, kind["buttons"][name], 1 if down else 0)
        if name in kind["triggers"]:
            emit(fd, EV_ABS, kind["triggers"][name], kind["trigger_max"] if down else 0)
    sync(fd)


def supported(kind, name):
    if name in kind.get("chords", {}):
        return True
    return name in HAT or name in kind["buttons"] or name in kind["triggers"]


def create(kind, usb=False):
    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_ABS)
    for code in sorted(set(kind["buttons"].values()) | set(kind.get("extra_keys", ()))):
        fcntl.ioctl(fd, UI_SET_KEYBIT, code)
    absmin = [0] * 64
    absmax = [0] * 64
    absflat = [0] * 64
    axes, lo, hi = kind["sticks"]
    for axis in axes:
        fcntl.ioctl(fd, UI_SET_ABSBIT, axis)
        absmin[axis] = lo
        absmax[axis] = hi
    for axis in kind["triggers"].values():
        fcntl.ioctl(fd, UI_SET_ABSBIT, axis)
        absmax[axis] = kind["trigger_max"]
    for axis in (ABS_HAT0X, ABS_HAT0Y):
        fcntl.ioctl(fd, UI_SET_ABSBIT, axis)
        absmin[axis] = -1
        absmax[axis] = 1
    name = (kind.get("usb_name", kind["name"]) if usb else kind["name"]).encode()
    bus, vendor, product, version = kind.get("usb_ids", kind["ids"]) if usb else kind["ids"]
    dev = struct.pack("80sHHHHI", name, bus, vendor, product, version, 0)
    dev += struct.pack("64i", *absmax) + struct.pack("64i", *absmin) + struct.pack("64i", *([0] * 64)) + struct.pack("64i", *absflat)
    os.write(fd, dev)
    fcntl.ioctl(fd, UI_DEV_CREATE)
    time.sleep(0.2)
    for axis in axes:
        emit(fd, EV_ABS, axis, (lo + hi + 1) // 2)
    sync(fd)
    return fd


def main():
    ap = argparse.ArgumentParser(description=USAGE, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kind", choices=sorted(KINDS), default="dualsense")
    ap.add_argument("--usb", action="store_true")
    ap.add_argument("--delay", type=float, default=2.0)
    ap.add_argument("--hold", type=float, default=2.0)
    ap.add_argument("steps", nargs="*")
    a = ap.parse_args()
    kind = KINDS[a.kind]
    for step in a.steps:
        names = step.split(":")[0]
        if names == "wait":
            continue
        for name in names.split("+"):
            if name not in STEP_NAMES:
                sys.exit("unknown step " + step)
            if not supported(kind, name):
                sys.exit("the %s pad has no %s" % (a.kind, name))
    fd = create(kind, a.usb)
    print("vpad: created " + a.kind, flush=True)
    try:
        time.sleep(a.delay)
        for step in a.steps:
            names, _, ms = step.partition(":")
            ms = int(ms) if ms else 120
            if names == "wait":
                time.sleep(ms / 1000)
                continue
            buttons = expand(kind, names.split("+"))
            for name in buttons:
                press(fd, kind, name, True)
                time.sleep(0.03)
            time.sleep(ms / 1000)
            for name in reversed(buttons):
                press(fd, kind, name, False)
            time.sleep(0.25)
            print("vpad: " + step, flush=True)
        time.sleep(a.hold)
    finally:
        fcntl.ioctl(fd, UI_DEV_DESTROY)
        os.close(fd)
        print("vpad: removed", flush=True)


if __name__ == "__main__":
    main()
