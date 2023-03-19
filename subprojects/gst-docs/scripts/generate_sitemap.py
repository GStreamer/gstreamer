#!/usr/bin/env python3
import os
import sys


if __name__ == "__main__":
    in_, out, index_md = sys.argv[1], sys.argv[2], sys.argv[3]
    with open(in_) as f:

        index = f.read()
        index = '\n'.join(l for l in index.splitlines())

        if sys.argv[4]:
            libs, plugins = sys.argv[4].split(os.pathsep), sorted(
                sys.argv[5].replace('\n', '').split(os.pathsep), key=lambda x: os.path.basename(x))
            index += '\n\tlibs.md'
            for lib in libs:
                if not lib:
                    continue
                index += "\n\t\t" + lib + '.json'
            index += '\n\tgst-index'
            for plugin in plugins:
                if not plugin:
                    continue
                fname = plugin
                if not fname.endswith('.json'):
                    fname += '.json'
                index += "\n\t\t" + fname

        index = '%s\n%s' % (index_md, index)

        with open(out, 'w') as fw:
            fw.write(index)
