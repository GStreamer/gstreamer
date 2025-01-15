#!/usr/bin/env python3
import os
import sys
from argparse import ArgumentParser
from pathlib import Path as P
import json

if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument('--input-sitemap', type=P)
    parser.add_argument('--output-sitemap', type=P)
    parser.add_argument('--markdown-index', type=P)
    parser.add_argument('--libs', type=str)
    parser.add_argument('--plugins', type=str)
    parser.add_argument('--plugin-configs', nargs='*', default=[])
    parser.add_argument('--lib-configs', nargs='*', default=[])

    args = parser.parse_args()

    in_ = args.input_sitemap
    out = args.output_sitemap
    index_md = args.markdown_index
    plugins = args.plugins
    plugin_configs = args.plugin_configs
    lib_configs = args.lib_configs

    with open(in_) as f:
        index = f.read()
        index = '\n'.join(line for line in index.splitlines())

        if args.libs is None:
            libs = []
        else:
            libs = args.libs.split(os.pathsep)
        for config in lib_configs:
            with open(config) as f:
                libs += json.load(f)
        plugins = plugins.replace('\n', '').split(os.pathsep)
        for config in plugin_configs:
            with open(config) as f:
                plugins += json.load(f)
        plugins = sorted(plugins, key=lambda x: os.path.basename(x))

        if libs:
            index += '\n\tlibs.md'
            for lib in libs:
                if not lib:
                    continue
                name = lib
                if not name.endswith('.json'):
                    name += '.json'
                index += "\n\t\t" + name
        if plugins:
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
