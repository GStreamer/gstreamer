#!/usr/bin/env python3

import os
import sys
import ssl
import zipfile
import hashlib
import tempfile
import subprocess
import http.client
import urllib.request

# Disable certificate checking because it always fails on Windows
# We verify the checksum anyway.
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

download_only = '--download-only' in sys.argv
if download_only:
    sys.argv.remove('--download-only')

version = sys.argv[1]
want_sha256 = sys.argv[3]

if sys.argv[2] == 'darwin':
    platform = 'mac'
    target = 'macos'
    ext = 'zip'
elif sys.argv[2] == 'linux':
    platform = 'linux'
    target = 'linux-x86_64'
    ext = 'tar.xz'
elif sys.argv[2] == 'windows':
    platform = 'windows'
    target = 'windows-X64'
    ext = 'exe'
else:
    raise RuntimeError('Unsupported platform:', sys.argv[2])

# Example URL: https://sdk.lunarg.com/sdk/download/1.4.341.0/mac/vulkansdk-macos-1.4.341.0.zip
BASENAME = f'vulkansdk-{target}-{version}.{ext}'
UPSTREAM_URL = f'https://sdk.lunarg.com/sdk/download/{version}/{platform}/' + BASENAME
GSTREAMER_URL = 'https://gstreamer.freedesktop.org/src/mirror/' + BASENAME

if download_only:
    tmp_dir = tempfile.TemporaryDirectory()
    source_dir = tmp_dir.name
else:
    source_dir = os.path.join(os.environ['MESON_SOURCE_ROOT'],
                              os.environ['MESON_SUBDIR'])
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


def print_progress(done, total):
    mib = 1024 * 1024
    if not total:
        print(f'\r{done // mib} MiB', end='', flush=True)
        return
    width = 40
    filled = width * done // total
    gauge = '#' * filled + '-' * (width - filled)
    print(f'\r[{gauge}] {100 * done // total:3d}% '
          f'{done // mib}/{total // mib} MiB', end='', flush=True)


def download(req):
    # meson captures our output, don't flood its log with \r updates
    show_progress = sys.stdout.isatty()
    data = bytearray()
    try:
        with urllib.request.urlopen(req, context=ctx) as f:
            total = int(f.headers.get('Content-Length') or 0)
            while chunk := f.read(1024 * 1024):
                data += chunk
                if show_progress:
                    print_progress(len(data), total)
    finally:
        if show_progress and data:
            print()
    # Unlike read(), read(amt) returns b'' on a truncated body instead of
    # raising IncompleteRead.
    if total and len(data) < total:
        raise http.client.IncompleteRead(data, total - len(data))
    return data


if not verify_download(dest_path):
    for url in (GSTREAMER_URL, UPSTREAM_URL):
        # sdk.lunarg.com returns 403 if we don't have a user agent
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        print(f'Downloading {url} to {BASENAME}')
        try:
            data = download(req)
        # A connection dropped mid-body raises IncompleteRead or
        # ConnectionResetError, not URLError.
        except (OSError, http.client.HTTPException) as ex:
            print(ex)
            print(f'Failed to download from {url}, trying next mirror...')
            continue
        found_sha256 = hashlib.sha256(data).hexdigest()
        if found_sha256 != want_sha256:
            print(f'SHA256 of {url} was {found_sha256} '
                  f'instead of {want_sha256}')
            continue
        with open(dest_path, 'wb') as d:
            d.write(data)
        break
    else:
        curdir = os.path.dirname(sys.argv[0])
        print(f'Couldn\'t download {BASENAME}! Try downloading it manually and placing it into {curdir}')
        sys.exit(1)

if download_only:
    print(f'{BASENAME} downloaded and verified')
    sys.exit(0)

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
