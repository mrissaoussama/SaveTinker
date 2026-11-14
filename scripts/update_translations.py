#!/usr/bin/env python3
"""Collects tr/trf/trc strings from app/ and updates app/lang/<code>.json.

Existing translations are kept, new strings are added with an empty value (shown in English until
translated), and strings no longer used are dropped.

Usage: scripts/update_translations.py [language code ...]
"""
import codecs
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CALL = re.compile(r'\btr(?:[fc]|_noop)?\(\s*("(?:[^"\\\n]|\\.)*")')


def collect():
    strings = set()
    for path in sorted((ROOT / 'app').glob('*.cpp')):
        for literal in CALL.findall(path.read_text(encoding='utf-8')):
            strings.add(codecs.escape_decode(literal[1:-1].encode('utf-8'))[0].decode('utf-8'))
    return sorted(strings, key=str.casefold)


def update(code, strings):
    path = ROOT / 'app' / 'lang' / (code + '.json')
    existing = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
    merged = {text: existing.get(text, '') for text in strings}
    path.write_text(json.dumps(merged, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    missing = sum(1 for value in merged.values() if not value)
    print(f'{path.relative_to(ROOT)}: {len(merged)} strings, {missing} untranslated')


def main():
    strings = collect()
    codes = sys.argv[1:] or [p.stem for p in (ROOT / 'app' / 'lang').glob('*.json')]
    for code in codes:
        update(code, strings)


if __name__ == '__main__':
    main()
