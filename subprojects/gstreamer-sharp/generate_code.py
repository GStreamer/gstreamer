#!/usr/bin/env python3

import argparse
import glob
import os
import re
import shutil
import subprocess


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--api-raw")
    parser.add_argument("--gapi-fixup")
    parser.add_argument("--metadata")
    parser.add_argument("--gapi-codegen")
    parser.add_argument("--abi-includes", default="")
    parser.add_argument("--abi-cs-usings", default="")
    parser.add_argument("--assembly-name")
    parser.add_argument("--extra-includes", action='append', default=[])
    parser.add_argument("--out")
    parser.add_argument("--files")
    parser.add_argument("--symbols")
    parser.add_argument("--schema")
    parser.add_argument("--fake", action='store_true')

    opts = parser.parse_args()
    if opts.fake:
        exit(0)

    api_xml = os.path.join(opts.out, os.path.basename(
        opts.api_raw).replace('.raw', '.xml'))

    shutil.copyfile(opts.api_raw, api_xml)

    if shutil.which('mono'):
        launcher = ['mono', '--debug']
    else:
        launcher = []

    cmd = [opts.gapi_fixup, "--api=" + api_xml]
    if opts.metadata:
        cmd += ["--metadata=" + opts.metadata]
    if opts.symbols:
        cmd.extend(['--symbols=' + opts.symbols])
    subprocess.check_call(launcher + cmd)

    cmd = [
        opts.gapi_codegen, '--generate', api_xml,
        '--outdir=' + opts.out,
        '--assembly-name=' + opts.assembly_name,
        '--glue-includes=' + opts.abi_includes,
        '--abi-c-filename=' +
        os.path.join(opts.out, opts.assembly_name + "-abi.c"),
        '--abi-cs-filename=' +
        os.path.join(opts.out, opts.assembly_name + "-abi.cs"),
    ]

    if opts.schema:
        cmd += ['--schema=' + opts.schema]

    if opts.abi_cs_usings:
        cmd += ['--abi-cs-usings=' + opts.abi_cs_usings]

    cmd += ['-I' + i for i in opts.extra_includes]

    subprocess.check_call(launcher + cmd)

    # WORKAROUND: Moving files into the out directory with special names
    # as meson doesn't like path separator in output names.
    regex = re.compile('_')
    dirs = set()
    expected_files = set(opts.files.split(';'))
    for _f in expected_files:
        dirs.add(os.path.dirname(_f))

    generated = set(glob.glob(os.path.join('*/*.cs'), root_dir=opts.out))
    rcode = 0
    not_listed = generated - expected_files
    if not_listed:
        print("Following files were generated but not listed:\n    %s" %
              '\n    '.join(["'%s/%s'," % (m.split(os.path.sep)[-2], m.split(os.path.sep)[-1])
                             for m in not_listed]))
        rcode = 1

    not_generated = expected_files - generated
    if not_generated:
        print("Following files were listed but not generated:\n    %s" %
              '\n    '.join(["'%s/%s'," % (m.split(os.path.sep)[-2], m.split(os.path.sep)[-1])
                             for m in sorted(not_generated)]))
        rcode = 1

    if rcode == 1:
        generated = sorted(list(generated))
        print("List of files to use in `meson.build`:\n    %s" %
              '\n    '.join(["'%s/%s'," % (m.split(os.path.sep)[-2], m.split(os.path.sep)[-1])
                             for m in sorted(generated)]))

    exit(rcode)
