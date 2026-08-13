#!/usr/bin/env python3
"""Structural check for libretro_core_options.h.

The option table is a C array of retro_core_option_v2_definition, and
almost everything that can go wrong with it still compiles: a
description string accidentally concatenated onto the label reads as one
long label, a default that names no value reads as a valid pointer, a
duplicated or missing key is just a different array.  The compiler
cannot see any of it and neither can a build.  This can.

Field order: key, desc, desc_categorized, info, info_categorized,
category_key, values[], default_value.
"""
import os, re, sys

# resolve relative to this file, not the caller's working directory: a
# checker that only runs from the repository root is a checker that gets
# skipped
PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    '..', '..', 'sys', 'libretro', 'libretro_core_options.h')
LABEL_MAX = 64

def literals(text):
    """the C string literals in a field, before concatenation"""
    return re.findall(r'"((?:[^"\\]|\\.)*)"', text)

def main():
    src = open(PATH).read()
    # the file mixes three-space and tab indentation for entry openers -
    # a splitter that knows only one of them silently drops the others,
    # which is how an option went missing while everything still built
    entries = re.split(r'\n(?:   |\t)\{\n', src)[1:]
    fails, keys = [], []
    for e in entries:
        head = re.split(r'\n(?:   |\t)\},', e)[0]
        lines = head.split('\n')
        km = re.match(r'\s*"([a-z0-9_]+)",\s*$', lines[0])
        if not km:
            continue                      # not an option entry
        key = km.group(1)
        keys.append(key)

        # field 2, the label: everything up to the next line ending in a
        # comma at field indentation
        rest, label_lines = lines[1:], []
        for ln in rest:
            label_lines.append(ln)
            if ln.rstrip().endswith(','):
                break
        label_txt = '\n'.join(label_lines)
        parts = literals(label_txt)
        label = ''.join(parts)
        if len(parts) > 1:
            fails.append('%s: label is %d concatenated literals - a '
                'description has leaked into it: "%s..."'
                % (key, len(parts), label[:60]))
        elif len(label) > LABEL_MAX:
            fails.append('%s: label is %d chars, over %d - probably leaked text'
                % (key, len(label), LABEL_MAX))

        # No field-count check here: a miscount is exactly the "too many
        # initializers" the compiler already reports, and a counter that
        # has to understand C string concatenation to get it right is
        # more likely to be wrong than the thing it checks.  This script
        # is for what the compiler cannot see; the build covers the rest,
        # which is why the build must actually recompile the consumer.

        # values table and default
        vm = re.search(r'\{\n((?:\s*\{.*\n)+?)\s*\},\n\s*("(?:[^"\\]|\\.)*")', head)
        if vm:
            vals = re.findall(r'\{\s*"([^"]*)"', vm.group(1))
            dflt = vm.group(2)[1:-1]
            if not vals:
                fails.append('%s: empty values table' % key)
            elif dflt not in vals:
                fails.append('%s: default "%s" is not one of the values %s'
                    % (key, dflt, vals))
            if '{ NULL, NULL }' not in vm.group(1) and 'NULL, NULL' not in vm.group(1):
                fails.append('%s: values table is not NULL-terminated' % key)

    dupes = set(k for k in keys if keys.count(k) > 1)
    if dupes:
        fails.append('duplicate keys: %s' % sorted(dupes))

    declared = len(re.findall(r'\n\s+"doom_[a-z0-9_]+",\n', src))
    if len(keys) != declared:
        fails.append('parsed %d entries but the file declares %d keys - the '
            'parser is missing some' % (len(keys), declared))
    print('  %d option entries checked' % len(keys))
    for f in fails:
        print('  FAIL %s' % f)
    print('  %s' % ('FAIL' if fails else 'option table is structurally sound: PASS'))
    return 1 if fails else 0

if __name__ == '__main__':
    sys.exit(main())
