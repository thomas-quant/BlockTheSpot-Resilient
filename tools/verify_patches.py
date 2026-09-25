#!/usr/bin/env python3
"""Validate the configured SPA patches, not native hook installation or playback.

Usage: verify_patches.py <config.ini> <extracted-xpui-dir>
Mirrors runtime ordering, unique matches, paired-patch atomicity and write bounds.
Never modifies the input bundles. Native installation is checked separately by CI.
"""
import configparser
import re
import sys
from pathlib import Path


def parse_sig(text):
    tokens = text.split()
    if not tokens or len(text.encode('utf-8')) >= 1023:
        raise ValueError('empty or oversized signature')
    if any(not re.fullmatch(r'[0-9a-fA-F]{2}|\?\?', token) for token in tokens):
        raise ValueError('invalid signature byte')
    return [None if token == '??' else int(token, 16) for token in tokens]


def matches(data, signature):
    pattern = b''.join(b'.' if byte is None else re.escape(bytes([byte])) for byte in signature)
    # Lookahead includes overlapping matches. Two are enough to reject ambiguity.
    found = []
    for match in re.finditer(b'(?=' + pattern + b')', data, re.DOTALL):
        found.append(match.start())
        if len(found) == 2:
            break
    return found


def find(data, signature):
    found = matches(data, signature) if signature else []
    return found[0] if found else -1


def numbered(cp, section, maximum):
    if not cp.has_section(section):
        raise ValueError(f'missing section [{section}]')
    entries = {int(k): v for k, v in cp.items(section) if k.isdigit()}
    if not entries or sorted(entries) != list(range(1, len(entries) + 1)) or len(entries) > maximum:
        raise ValueError(f'[{section}] needs consecutive entries 1..N (maximum {maximum})')
    if any(not value or len(value.encode('utf-8')) >= 49 for value in entries.values()):
        raise ValueError(f'[{section}] has an empty or oversized name')
    return [entries[i] for i in sorted(entries)]


def apply_patch(cp, patch, data):
    if not cp.has_section(patch):
        raise ValueError('patch section missing')
    writes = []
    for i in (1, 2):
        key = f'Signature_{i}'
        if not cp.has_option(patch, key):
            if i == 1 or cp.has_option(patch, f'Value_{i}') or cp.has_option(patch, f'Offset_{i}'):
                raise ValueError(f'missing {key}')
            break
        signature = parse_sig(cp.get(patch, key))
        found = matches(data, signature)
        if len(found) != 1:
            raise ValueError(f'{key}: expected one match, found {"2+" if len(found) > 1 else 0}')
        value_text = cp.get(patch, f'Value_{i}')
        if '??' in value_text:
            raise ValueError('replacement cannot contain wildcards')
        value = bytes(parse_sig(value_text))
        offset = int(cp.get(patch, f'Offset_{i}', fallback='0'), 10)
        start = found[0] + offset
        if offset < 0 or start + len(value) > len(data):
            raise ValueError(f'Value_{i}: write outside returned file bytes')
        writes.append((start, value))
    if len(writes) == 2:
        (a, av), (b, bv) = writes
        if a < b + len(bv) and b < a + len(av):
            raise ValueError('paired writes overlap')
    # Validate all signatures before applying either half, just like the DLL.
    patched = bytearray(data)
    for start, value in writes:
        patched[start:start + len(value)] = value
    return bytes(patched)


def verify(cfg_path, spa_dir):
    cp = configparser.ConfigParser(interpolation=None)
    cp.optionxform = str
    try:
        with open(cfg_path, encoding='utf-8-sig') as config:
            cp.read_file(config)
        if not cp.has_section('Buffer_modify'):
            raise ValueError('missing [Buffer_modify]')
        if cp.get('Buffer_modify', 'Enable', fallback='0') != '1':
            print('Buffer_modify disabled; JS patches NOT verified.')
            return 0
        targets = numbered(cp, 'Buffer_modify', 10)
        root = Path(spa_dir).resolve()
        checked = 0
        failures = []
        for target in targets:
            try:
                path = (root / target).resolve()
                if not path.is_relative_to(root):
                    raise ValueError('target escapes bundle directory')
                data = path.read_bytes()
                patches = numbered(cp, target, 10)
                for patch in patches:
                    try:
                        data = apply_patch(cp, patch, data)
                        checked += 1
                        print(f'OK    {target} / {patch}')
                    except (ValueError, configparser.Error) as error:
                        failures.append(f'{target} / {patch}: {error}')
            except (OSError, ValueError) as error:
                failures.append(f'{target}: {error}')
        if failures:
            print('\nBROKEN:')
            for failure in failures:
                print(f'FAIL  {failure}')
            return 1
        print(f'\nAll {checked} JS patch group(s) have unique matches and bounded writes.')
        print('Static JS check only; native hook installation and audio blocking are NOT verified here.')
        return 0
    except (OSError, ValueError, configparser.Error) as error:
        print(f'FAIL  config: {error}')
        return 1


def main():
    if len(sys.argv) != 3:
        print('usage: verify_patches.py <config.ini> <xpui-dir>', file=sys.stderr)
        return 2
    return verify(*sys.argv[1:])


if __name__ == '__main__':
    sys.exit(main())
