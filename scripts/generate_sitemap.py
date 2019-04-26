#!/usr/bin/env python3
import os
import sys


if __name__ == "__main__":
    in_, out, index_md = sys.argv[1], sys.argv[2], sys.argv[3]
    with open(in_) as f:
        index = f.read()
        if sys.argv[4]:
            index = '\n'.join('\t' + l for l in index.splitlines())
            libs, plugins = sys.argv[4].split(os.pathsep), sorted(
                sys.argv[5].split(os.pathsep), key=lambda x: os.path.basename(x))
            index += '\n	api.md\n		libs.md'
            for lib in libs:
                if not lib:
                    continue
                index += "\n			" + lib + '.json'
            index += '\n		plugins_doc.md'
            for plugin in plugins:
                if not plugin:
                    continue
                index += "\n			" + plugin + '.json'
            index = '%s\n%s' % (index_md, index)
        with open(out, 'w') as fw:
            fw.write(index)
