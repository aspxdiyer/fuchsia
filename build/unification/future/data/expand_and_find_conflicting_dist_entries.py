#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Reads the contents of a manifest file generated by the build and verifies
   that there are no collisions among destination paths.
   '''

import argparse
import collections
import filecmp
import functools
import json
import sys

Entry = collections.namedtuple('Entry', ['source', 'destination', 'label'])

# List of destination paths for which conflicting entries are acceptable as long
# as the source files are identical.
# This is mostly use to soft-transition zbi contents.
DUPLICATION_EXCEPTIONS = [
    # TODO(57360): remove exception after transitioning zxcrypt config from
    # build argument to board label.
    'config/zxcrypt',
]


def expand(items):
    '''Reads metadata produced by GN and expands file references found within
       that metadata.
       See distribution_manifest.gni for a description of the metadata format.
       '''
    entries = []
    for item in items:
        if 'source' in item:
            entries.append(item)
        elif 'file' in item:
            with open(item['file'], 'r') as data_file:
                data = json.load(data_file)
            for entry in data:
                entry['label'] = item['label']
                entries.append(entry)
    return [Entry(**e) for e in entries]


def sources_are_different(entries):
    '''Returns true if the given entries do not all point to identical source
       files.'''
    sources = [entry.source for entry in entries]
    return any(not filecmp.cmp(sources[0], f) for f in sources[1:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--input', help='Path to original manifest', required=True)
    parser.add_argument(
        '--output', help='Path to the updated manifest', required=True)
    args = parser.parse_args()

    with open(args.input, 'r') as input_file:
        contents = json.load(input_file)

    entries = expand(contents)
    entries_by_dest = {
        d: set(e for e in entries if e.destination == d) for d in set(
            e.destination for e in entries)
    }

    # Filter entries for which is is ok to have duplication if the sources are
    # the same.
    for exception in DUPLICATION_EXCEPTIONS:
        if not exception in entries_by_dest:
            continue
        duplicates = list(entries_by_dest[exception])
        if sources_are_different(duplicates):
            # Treat this as a normal conflict.
            continue
        # This is an exception! Select the first entry as canon, and remove
        # the others.
        canon = duplicates[0]
        entries_by_dest[exception] = set([canon])
        for entry in duplicates[1:]:
            entries.remove(entry)

    conflicts = {d: e for d, e in entries_by_dest.items() if len(e) > 1}
    # Only report a conflict if the source files differ.
    # TODO(45680): remove this additional filtering when dependency trees are
    # cleaned up and //build/package.gni has gone the way of the dodo.
    conflicts = {
        d: e
        for d, e in conflicts.items()
        if len(set(entry.source for entry in e)) >= 2
    }
    if conflicts:
        for destination in conflicts:
            print('Conflicts for path ' + destination + ':')
            for conflict in conflicts[destination]:
                print(' - ' + conflict.source)
                print('   from ' + conflict.label)
        print('Error: conflicting distribution entries!')
        return 1

    with open(args.output, 'w') as output_file:
        json.dump(
            [e._asdict() for e in sorted(entries)],
            output_file,
            indent=2,
            sort_keys=True,
            separators=(',', ': '))

    return 0


if __name__ == '__main__':
    sys.exit(main())
