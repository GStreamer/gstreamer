#!/usr/bin/python3
import os, subprocess, sys

if __name__ == "__main__":
    diffsdir = 'plugins-cache-diffs'
    os.makedirs(diffsdir, exist_ok=True)
    res = 0
    try:
        subprocess.check_call(['git', 'diff', '--quiet'] )
    except subprocess.CalledProcessError:
        diffname = os.path.join(diffsdir, 'plugins_cache.diff')
        res += 1
        with open(diffname, 'w') as diff:
            subprocess.check_call(['git', 'diff'], stdout=diff)
            print('\033[91mYou have a diff in the documentation cache. Please update with:\033[0m')
            print('     $ curl %s/%s | git apply -' % (os.environ['CI_ARTIFACTS_URL'], diffname.replace('../', '')))

    if res != 0:
        print('(note that it might take a few minutes for artefacts to be available on the server)\n')
        sys.exit(res)