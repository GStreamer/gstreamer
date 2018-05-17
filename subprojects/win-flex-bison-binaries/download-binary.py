#!/usr/bin/env python3

import os
import sys
import ssl
import zipfile
import hashlib
import urllib.request

# Disable certificate checking because it always fails on Windows
# We verify the checksum anyway.
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

base_url = 'https://sourceforge.net/projects/winflexbison/files/win_flex_bison-{}.zip'
url = base_url.format(sys.argv[1])
zip_sha256 = sys.argv[2]
source_dir = os.path.join(os.environ['MESON_SOURCE_ROOT'], os.environ['MESON_SUBDIR'])
dest = os.path.basename(url)
dest_path = os.path.join(source_dir, dest)

def get_sha256(zipf):
    hasher = hashlib.sha256()
    with open(zipf, 'rb') as f:
        hasher.update(f.read())
    return hasher.hexdigest()

if os.path.isfile(dest_path):
    found_sha256 = get_sha256(dest_path)
    if found_sha256 == zip_sha256:
        print('{} already downloaded'.format(dest))
        sys.exit(0)
    else:
        print('{} checksum mismatch, redownloading'.format(dest))

print('Downloading {} to {}'.format(url, dest))
with open(dest_path, 'wb') as d:
    f = urllib.request.urlopen(url, context=ctx)
    d.write(f.read())

found_sha256 = get_sha256(dest_path)
if found_sha256 != zip_sha256:
    print('SHA256 of downloaded file {} was {} instead of {}'
          ''.format(dest, found_sha256, zip_sha256))
    sys.exit(1)

print('Extracting {}'.format(dest))
zf = zipfile.ZipFile(dest_path, "r")
zf.extractall(path=source_dir)
