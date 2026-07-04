#!/usr/bin/env python3
"""Verify that config.ini's Buffer_modify JS signatures still match the current
Spotify bundles.

Usage: verify_patches.py <config.ini> <extracted-xpui-dir>

Parses [Buffer_modify] -> target files -> patch sections, and checks each
Signature_1 (BlockTheSpot hex with `??` wildcards) still occurs in its target
file. Exits 0 if every signature matches, 1 otherwise, printing which broke.
Used by the spotify-watch workflow to detect when a Spotify update has shifted
the code out from under a patch, so it can alert instead of silently shipping a
no-op patch.
"""
import configparser
import os
import sys


def parse_sig(hexstr):
    out = []
    for tok in hexstr.split():
        out.append(None if tok == "??" else int(tok, 16))
    return out


def find(data, sig):
    n = len(sig)
    if n == 0:
        return -1
    for i in range(len(data) - n + 1):
        for j, b in enumerate(sig):
            if b is not None and data[i + j] != b:
                break
        else:
            return i
    return -1


def main():
    if len(sys.argv) != 3:
        print("usage: verify_patches.py <config.ini> <xpui-dir>", file=sys.stderr)
        return 2
    cfg_path, spa_dir = sys.argv[1], sys.argv[2]

    cp = configparser.ConfigParser(interpolation=None, strict=False)
    cp.optionxform = str  # preserve key case (Signature_1)
    cp.read(cfg_path, encoding="utf-8")

    if not cp.has_section("Buffer_modify") or cp.get("Buffer_modify", "Enable", fallback="0") != "1":
        print("Buffer_modify disabled or absent; nothing to verify.")
        return 0

    targets = [v for k, v in cp.items("Buffer_modify") if k.isdigit()]
    fails, checked = [], 0

    for tf in targets:
        path = os.path.join(spa_dir, tf)
        if not os.path.exists(path):
            fails.append((tf, "<file>", "target file missing from spa"))
            continue
        data = open(path, "rb").read()
        if not cp.has_section(tf):
            continue
        for pn in [v for k, v in cp.items(tf) if k.isdigit()]:
            if not cp.has_section(pn):
                fails.append((tf, pn, "patch section missing"))
                continue
            sighex = cp.get(pn, "Signature_1", fallback=None)
            if not sighex:
                fails.append((tf, pn, "no Signature_1"))
                continue
            checked += 1
            if find(data, parse_sig(sighex)) < 0:
                fails.append((tf, pn, "signature no longer matches"))
            else:
                print(f"OK    {tf} / {pn}")

    if fails:
        print("\nBROKEN:")
        for tf, pn, why in fails:
            print(f"FAIL  {tf} / {pn}: {why}")
        return 1

    print(f"\nAll {checked} patch signature(s) still match.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
