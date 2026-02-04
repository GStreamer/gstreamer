#!/usr/bin/env python3

import os
import sys
import ssl
import shutil
import zipfile
import hashlib
import subprocess
import urllib.request
import urllib.error

# Disable certificate checking because it always fails on Windows
# We verify the checksum anyway.
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

version = sys.argv[1]
platform = sys.argv[2]
want_sha256 = sys.argv[3]

if sys.argv[2] == 'darwin':
    platform = 'macos'
    ext = 'zip'
elif sys.argv[2] == 'linux':
    platform = 'linux-x86_64'
    ext = 'tar.xz'
elif sys.argv[2] == 'windows':
    platform = 'windows-X64'
    ext = 'exe'
else:
    raise RuntimeError('Unsupported platform:', sys.argv[2])

BASENAME = f'vulkansdk-{platform}-{version}.{ext}'
UPSTREAM_URL = f'https://sdk.lunarg.com/sdk/download/{platform}/{version}/' + BASENAME
GSTREAMER_URL = 'https://gstreamer.freedesktop.org/src/mirror/' + BASENAME

source_dir = os.path.join(os.environ['MESON_SOURCE_ROOT'], os.environ['MESON_SUBDIR'])
dest_path = os.path.join(source_dir, BASENAME)


def get_sha256(fname):
    hasher = hashlib.sha256()
    with open(fname, 'rb') as f:
        hasher.update(f.read())
    return hasher.hexdigest()


def verify_download(dest_path):
    if os.path.isfile(dest_path):
        found_sha256 = get_sha256(dest_path)
        if found_sha256 == want_sha256:
            print(f'{BASENAME} already downloaded')
            return True
        print(f'{BASENAME} checksum mismatch, redownloading')
    return False


if not verify_download(dest_path):
    for url in (GSTREAMER_URL, UPSTREAM_URL):
        print(f'Downloading {url} to {BASENAME}')
        try:
            with open(dest_path, 'wb') as d:
                f = urllib.request.urlopen(url, context=ctx)
                d.write(f.read())
            break
        except urllib.error.URLError as ex:
            print(ex)
            print('Failed to download from {url}, trying mirror...')
            continue
    else:
        curdir = os.path.dirname(sys.argv[0])
        print(f'Couldn\'t download {url}! Try downloading it manually and placing it into {curdir}')
        sys.exit(1)
    found_sha256 = get_sha256(dest_path)
    if found_sha256 != want_sha256:
        print(f'SHA256 of downloaded file {BASENAME} was {found_sha256} instead of {want_sha256}')
        sys.exit(1)

install_dir = os.path.join(source_dir, f'sdk-{version}')
if os.path.exists(install_dir):
    print(f'Extracted SDK already exists in {os.path.basename(install_dir)}')
    sys.exit(0)

if ext == 'zip':
    print(f'Extracting {BASENAME}')
    zf = zipfile.ZipFile(dest_path, "r")
    zf.extractall(path=source_dir)
    installer = f'{source_dir}/vulkansdk-macOS-{version}.app/Contents/MacOS/vulkansdk-macOS-{version}'
    os.chmod(installer, 0o755)
    subprocess.run([
        installer,
        '--root', install_dir,
        '--accept-licenses',
        '--default-answer',
        '--confirm-command',
        'install',
        'copy_only=1',
    ])
else:
    raise NotImplementedError
