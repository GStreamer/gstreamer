#!/usr/bin/env python3

import os
import sys
import ssl
import tarfile
import hashlib
import urllib.request
import urllib.error

# Disable certificate checking because it requires custom Python setup on macOS
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

EXTRACTDIR = 'bison-{}-macos-{}'
BASENAME = '{}.tar.bz2'.format(EXTRACTDIR)
GSTREAMER_URL = 'https://gstreamer.freedesktop.org/src/mirror/{}'

version = sys.argv[1]
arch = sys.argv[2]
tar_sha256 = sys.argv[3]
source_dir = os.path.join(os.environ['MESON_SOURCE_ROOT'], os.environ['MESON_SUBDIR'])
dest = BASENAME.format(version, arch)
dest_path = os.path.join(source_dir, dest)
extract_path = EXTRACTDIR.format(version, arch)


def get_sha256(tarf):
    hasher = hashlib.sha256()
    with open(tarf, 'rb') as f:
        hasher.update(f.read())
    return hasher.hexdigest()


def download():
    for url in (GSTREAMER_URL.format(dest),):
        print('Downloading {} to {}'.format(url, dest), file=sys.stderr)
        try:
            with open(dest_path, 'wb') as d:
                f = urllib.request.urlopen(url, context=ctx)
                d.write(f.read())
            break
        except urllib.error.URLError as ex:
            print(ex, file=sys.stderr)
            print('Failed to download from {!r}, trying mirror...'.format(url), file=sys.stderr)
            continue
    else:
        curdir = os.path.dirname(sys.argv[0])
        print('Couldn\'t download {!r}! Try downloading it manually and '
              'placing it into {!r}'.format(dest, curdir), file=sys.stderr)


def print_extract_dir():
    'Print the extracted directory name'
    print(extract_path, end='')


if os.path.isfile(dest_path):
    found_sha256 = get_sha256(dest_path)
    if found_sha256 == tar_sha256:
        if os.path.isdir(os.path.join(source_dir, extract_path)):
            print('{} already downloaded and extracted'.format(dest), file=sys.stderr)
            print_extract_dir()
            sys.exit(0)
    else:
        print('{} checksum mismatch, redownloading'.format(dest), file=sys.stderr)
        download()
else:
    download()

found_sha256 = get_sha256(dest_path)
if found_sha256 != tar_sha256:
    print('SHA256 of downloaded file {} was {} instead of {}'
          ''.format(dest, found_sha256, tar_sha256), file=sys.stderr)
    sys.exit(1)

print('Extracting {}'.format(dest), file=sys.stderr)
tf = tarfile.open(dest_path, "r")
tf.extractall(path=source_dir)
print_extract_dir()
