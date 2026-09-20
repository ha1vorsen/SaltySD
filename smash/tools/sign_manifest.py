#!/usr/bin/env python3
"""Signs SaltySD update manifests.

  sign_manifest.py keygen <key>
  sign_manifest.py check-key --key <key> --key-id N --keys-header <update_keys.h>
  sign_manifest.py sign --key <key> --key-id N --channel stable|dirty (--version X.Y.Z | --commit C)
                        --out <dir> --file <tids> <plugin.3gx> [--file ...]
"""

import argparse
import datetime
import hashlib
import os
import re
import shutil
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization

MAX_MANIFEST = 4096
TID = re.compile(r'^[0-9A-F]{8}$')
NAME = re.compile(r'^[A-Za-z0-9_.-]{1,63}$')
VERSION = re.compile(r'^[0-9]{1,9}\.[0-9]{1,9}\.[0-9]{1,9}$')
COMMIT = re.compile(r'^[0-9a-z-]{1,40}$')
ISSUED = re.compile(r'^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$')


def load_key(path):
    raw = open(path, 'rb').read()
    if len(raw) != 32:
        sys.exit('%s is not a 32-byte Ed25519 private key' % path)
    return Ed25519PrivateKey.from_private_bytes(raw)


def public_bytes(key):
    return key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def keygen(args):
    if os.path.exists(args.path):
        sys.exit('%s already exists; refusing to overwrite a key' % args.path)
    key = Ed25519PrivateKey.generate()
    raw = key.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw,
                            serialization.NoEncryption())
    fd = os.open(args.path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as out:
        out.write(raw)
    print(', '.join('0x%02X' % b for b in public_bytes(key)))


def check_key(args):
    pub = public_bytes(load_key(args.key))
    src = open(args.keys_header).read()
    entry = re.search(r'\{\s*%d\s*,[^{]*\{([^}]*)\}' % args.key_id, src)
    if not entry:
        sys.exit('key id %d is not in %s' % (args.key_id, args.keys_header))
    built_in = bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', entry.group(1)))
    if built_in != pub:
        sys.exit('mismatch: %s is not key id %d' % (args.key, args.key_id))
    print('match')


def sign(args):
    key = load_key(args.key)
    if args.key_id < 1:
        sys.exit('--key-id starts at 1')
    if args.channel == 'stable':
        if not args.version or args.commit or not VERSION.match(args.version):
            sys.exit('stable needs --version X.Y.Z and no --commit')
        identity = 'version ' + args.version
    else:
        if not args.commit or args.version or not COMMIT.match(args.commit):
            sys.exit('dirty needs --commit (0-9 a-z -, up to 40) and no --version')
        identity = 'commit ' + args.commit

    if args.issued:
        if not ISSUED.match(args.issued):
            sys.exit('--issued must look like YYYY-MM-DDTHH:MM:SSZ')
        issued = args.issued
    else:
        issued = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')

    lines = ['saltysd-manifest 1', 'key %d' % args.key_id, 'channel %s' % args.channel, identity,
             'issued ' + issued]
    seen = set()
    for tids, path in args.file:
        tid_list = tids.upper().split(',')
        if not all(TID.match(t) for t in tid_list):
            sys.exit('bad title ID list: %s' % tids)
        if seen & set(tid_list):
            sys.exit('a title ID is listed twice')
        seen |= set(tid_list)
        name = os.path.basename(path)
        if not NAME.match(name):
            sys.exit('file name must be 1-63 of A-Z a-z 0-9 _ . -: %s' % name)
        data = open(path, 'rb').read()
        lines.append('file %s %s %d %s' % (','.join(tid_list), name, len(data),
                                           hashlib.sha512(data).hexdigest()))

    text = ('\n'.join(lines) + '\n').encode('ascii')
    if len(text) > MAX_MANIFEST:
        sys.exit('manifest is %d bytes; the plugin reads at most %d' % (len(text), MAX_MANIFEST))

    os.makedirs(args.out, exist_ok=True)
    for _, path in args.file:
        dest = os.path.join(args.out, os.path.basename(path))
        if os.path.abspath(dest) != os.path.abspath(path):
            shutil.copyfile(path, dest)
    open(os.path.join(args.out, 'manifest.txt'), 'wb').write(text)
    open(os.path.join(args.out, 'manifest.sig'), 'wb').write(key.sign(text))
    print(text.decode('ascii'), end='')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)

    p = sub.add_parser('keygen')
    p.add_argument('path')
    p.set_defaults(run=keygen)

    p = sub.add_parser('check-key')
    p.add_argument('--key', required=True)
    p.add_argument('--key-id', type=int, required=True)
    p.add_argument('--keys-header', required=True)
    p.set_defaults(run=check_key)

    p = sub.add_parser('sign')
    p.add_argument('--key', required=True)
    p.add_argument('--key-id', type=int, required=True)
    p.add_argument('--version')
    p.add_argument('--commit')
    p.add_argument('--channel', choices=('stable', 'dirty'), required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--issued')
    p.add_argument('--file', nargs=2, action='append', required=True, metavar=('TIDS', 'PLUGIN'))
    p.set_defaults(run=sign)

    args = parser.parse_args()
    args.run(args)


if __name__ == '__main__':
    main()
