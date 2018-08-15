#!/usr/bin/env python3

import os
import sys
import tarfile

if __name__ == "__main__":
    files = sys.argv[1]
    outname = sys.argv[2]
    readme = sys.argv[3]

    tar = tarfile.open(outname, 'w:xz')
    os.chdir(files)
    tar.add(os.path.curdir)
    os.chdir(os.path.dirname(readme))
    tar.add(os.path.basename(readme))
    tar.close()