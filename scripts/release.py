#!/usr/bin/env python3

import os
import sys
import tarfile

if __name__ == "__main__":
    files = sys.argv[1]
    release_name = sys.argv[2]
    readme = sys.argv[3]
    outname = release_name + '.tar.xz'

    print("Generating %s" % os.path.realpath(os.path.join(os.path.curdir, outname)), file=sys.stderr)
    tar = tarfile.open(outname, 'w:xz')
    tar.add(files, release_name)
    os.chdir(os.path.dirname(readme))
    tar.add(os.path.basename(readme), os.path.join(release_name, os.path.basename(readme)))
    tar.close()