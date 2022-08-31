#!/usr/bin/python3
import os
import subprocess
import sys
import argparse

PARSER = argparse.ArgumentParser()
PARSER.add_argument('name', default="documentation cache", nargs="?")

if __name__ == "__main__":
    opts = PARSER.parse_args()

    print(opts)
    diffsdir = 'diffs'
    os.makedirs(diffsdir, exist_ok=True)
    res = 0
    try:
        subprocess.check_call(['git', 'diff', '--quiet'])
    except subprocess.CalledProcessError:
        diffname = os.path.join(diffsdir, f"{opts.name.replace(' ', '_')}.diff")
        res += 1
        with open(diffname, 'w') as diff:
            subprocess.check_call(['git', 'diff'], stdout=diff)
            print(f'\033[91mYou have a diff in the {opts.name}. Please update with:\033[0m')
            print('     $ curl %s/%s | git apply -' %
                  (os.environ.get('CI_ARTIFACTS_URL', "NOT_RUNNING_ON_CI"), diffname.replace('../', '')))

    if res != 0:
        print('(note that it might take a few minutes for artefacts to be available on the server)\n')
        sys.exit(res)
