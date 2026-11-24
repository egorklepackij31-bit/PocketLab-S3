#!/usr/bin/env python3

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PINS_H = os.path.join(ROOT, "include", "pins.h")

RESERVED = {
    **{p: "SPI flash" for p in range(26, 33)},
    **{p: "PSRAM (octal)" for p in range(33, 38)},
    19: "USB D-",
    20: "USB D+",
    22: "не существует на S3",
    23: "не существует на S3",
    24: "не существует на S3",
    25: "не существует на S3",
    43: "UART0 TX (консоль)",
    44: "UART0 RX (консоль)",
}
STRAPPING = {0: "BOOT/strapping", 45: "VDD_SPI strapping", 46: "strapping"}

def eval_pins(use_psram: bool) -> dict:
    pins, stack = {}, []
    active = True
    with open(PINS_H, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("//", 1)[0].strip()
            if not line:
                continue
            if line.startswith("#if"):
                cond = None
                if "IX_USE_PSRAM" in line:
                    cond = use_psram
                stack.append((active, cond))
                active = active and (cond if cond is not None else True)
            elif line.startswith("#else"):
                parent, cond = stack[-1]
                active = parent and (not cond if cond is not None else True)
            elif line.startswith("#endif"):
                parent, _ = stack.pop()
                active = parent
            elif line.startswith("#define") and active:
                m = re.match(r"#define\s+(PIN_[A-Z0-9_]+)\s+(-?\d+)", line)
                if m:
                    pins[m.group(1)] = int(m.group(2))
    return pins

def check(name: str, pins: dict, psram: bool) -> int:
    errors = warnings = 0
    print(f"\n=== {name} ===")
    by_pin = {}
    for role, pin in pins.items():
        if pin < 0:
            continue
        if pin in by_pin:
            print(f"  ERROR  GPIO{pin}: {by_pin[pin]} и {role}")
            errors += 1
        else:
            by_pin[pin] = role

    for pin, role in sorted(by_pin.items()):
        note = ""
        if pin in RESERVED:
            if psram and 33 <= pin <= 37:
                print(f"  ERROR  GPIO{pin} {role}: {RESERVED[pin]} (PSRAM включён)")
                errors += 1
                continue
            if not (not psram and 33 <= pin <= 37):
                print(f"  ERROR  GPIO{pin} {role}: {RESERVED[pin]}")
                errors += 1
                continue
            note = "  (warn) PSRAM-пин, работает только при PSRAM=off"
            warnings += 1
        if pin in STRAPPING:
            note += f"  (warn) {STRAPPING[pin]}"
            warnings += 1
        print(f"  GPIO{pin:<3} {role:<18}{note}")

    if not psram:
        for p in (35, 36, 37):
            if p in by_pin:
                print(f"  NOTE   GPIO{p}: на N16R8 физически подключён к octal PSRAM;")
                print("         прошивка с PSRAM не запустит эти кнопки (см. docs/WIRING.md)")
    print(f"  итог: {len(by_pin)} пинов, ошибок {errors}, предупреждений {warnings}")
    return errors

def main() -> int:
    only = sys.argv[1].lower() if len(sys.argv) > 1 else "both"
    total = 0
    if only in ("both", "default"):
        total += check("incognitox (PSRAM off)", eval_pins(False), False)
    if only in ("both", "psram"):
        total += check("incognitox_psram (PSRAM on)", eval_pins(True), True)
    print("\nOK" if total == 0 else f"\nОШИБОК: {total}")
    return 0 if total == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
