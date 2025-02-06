#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright © 2018 Thibault Saunier <tsaunier@igalia.com>
#
# This library is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 2.1 of the License, or (at your option)
# any later version.
#
# This library is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this library.  If not, see <http://www.gnu.org/licenses/>.

import argparse
import json
import os
import sys
import re
import subprocess
from pathlib import Path as P
from argparse import ArgumentParser

from collections import OrderedDict
try:
    from collections.abc import Mapping
except ImportError:  # python <3.3
    from collections import Mapping

# Some project names need to be amended, to avoid conflicts with plugins.
# We also map gst to gstreamer to preserve existing links
PROJECT_NAME_MAP = {
    'gst': 'gstreamer',
    'app': 'applib',
    'rtp': 'rtplib',
    'rtsp': 'rtsplib',
    'webrtc': 'webrtclib',
    'mse': 'mselib',
    'va': 'valib',
    'vulkan': 'vulkanlib',
    'rtspserver': 'gst-rtsp-server',
    'validate': 'gst-devtools',
    'ges': 'gst-editing-services',
    'opencv': 'opencvlib',
}


def get_c_flags(deps, buildroot, uninstalled=True):
    if isinstance(deps, str):
        deps = [deps]
    env = os.environ.copy()
    if uninstalled:
        env['PKG_CONFIG_PATH'] = os.path.join(buildroot, 'meson-uninstalled')
    for dep in deps:
        res = subprocess.run(['pkg-config', '--cflags', dep], env=env, capture_output=True)
        if res.returncode == 0:
            return [res.stdout.decode().strip()]
    print("Failed to get cflags for:", ", ".join(deps), ", ignoring")
    return ''



class GstLibsHotdocConfGen:
    def __init__(self):
        parser = ArgumentParser()
        parser.add_argument('--srcdir', type=P)
        parser.add_argument('--builddir', type=P)
        parser.add_argument('--buildroot', type=P)
        parser.add_argument('--source_root', type=P)
        parser.add_argument('--gi_source_file', type=P)
        parser.add_argument('--gi_c_source_file', type=P)
        parser.add_argument('--gi_source_root', type=P)
        parser.add_argument('--c_source_file', type=P)
        parser.add_argument('--project_version')
        parser.add_argument('--gi_c_source_filters', nargs='*', default=[])
        parser.add_argument('--c_source_filters', nargs='*', default=[])
        parser.add_argument('--output', type=P)

        parser.parse_args(namespace=self, args=sys.argv[2:])

    def generate_libs_configs(self):
        conf_files = []

        with self.gi_c_source_file.open() as fd:
            gi_c_source_map = json.load(fd)

        with self.gi_source_file.open() as fd:
            gi_source_map = json.load(fd)

        if self.c_source_file is not None:
            with self.c_source_file.open() as fd:
                c_source_map = json.load(fd)
        else:
            c_source_map = {}

        for libname in gi_source_map.keys():
            gi_c_sources = gi_c_source_map[libname].split(os.pathsep)
            gi_sources = gi_source_map[libname].split(os.pathsep)

            project_name = PROJECT_NAME_MAP.get(libname, libname)

            if project_name == 'audio' and gi_sources[0].endswith('GstBadAudio-1.0.gir'):
                project_name = 'bad-audio'

            conf_path = self.builddir / f'{project_name}-doc.json'
            conf_files.append(str(conf_path))

            index_path = os.path.join(self.source_root, 'index.md')
            if not os.path.exists(index_path):
                index_path = os.path.join(self.source_root, libname, 'index.md')
                sitemap_path = os.path.join(self.source_root, libname, 'sitemap.txt')
                gi_index_path = os.path.join(self.source_root, libname, 'gi-index.md')
            else:
                sitemap_path = os.path.join(self.source_root, 'sitemap.txt')
                gi_index_path = os.path.join(self.source_root, 'gi-index.md')

            assert (os.path.exists(index_path))
            assert (os.path.exists(sitemap_path))
            if not os.path.exists(gi_index_path):
                gi_index_path = index_path

            gi_source_root = os.path.join(self.gi_source_root, libname)
            if not os.path.exists(gi_source_root):
                gi_source_root = os.path.join(self.gi_source_root)

            conf = {
                'sitemap': sitemap_path,
                'index': index_path,
                'gi_index': gi_index_path,
                'output': f'{project_name}-doc',
                'conf_file': str(conf_path),
                'project_name': project_name,
                'project_version': self.project_version,
                'gi_smart_index': True,
                'gi_order_generated_subpages': True,
                'gi_c_sources': gi_c_sources,
                'gi_c_source_roots': [
                    os.path.abspath(gi_source_root),
                    os.path.abspath(os.path.join(self.srcdir, '..',)),
                    os.path.abspath(os.path.join(self.builddir, '..',)),
                ],
                'include_paths': [
                    os.path.join(self.builddir),
                    os.path.join(self.srcdir),
                ],
                'gi_sources': gi_sources,
                'gi_c_source_filters': [str(s) for s in self.gi_c_source_filters],
                'extra_assets': os.path.join(self.srcdir, 'images'),
            }

            with conf_path.open('w') as f:
                json.dump(conf, f, indent=4)

        for libname in c_source_map.keys():
            c_sources = c_source_map[libname].split(os.pathsep)

            project_name = PROJECT_NAME_MAP.get(libname, libname)

            conf_path = self.builddir / f'{project_name}-doc.json'

            index_path = os.path.join(self.source_root, 'index.md')
            if not os.path.exists(index_path):
                index_path = os.path.join(self.source_root, libname, 'index.md')
                sitemap_path = os.path.join(self.source_root, libname, 'sitemap.txt')
                c_index_path = os.path.join(self.source_root, libname, 'c-index.md')
            else:
                sitemap_path = os.path.join(self.source_root, 'sitemap.txt')
                c_index_path = os.path.join(self.source_root, 'c-index.md')

            assert (os.path.exists(index_path))
            assert (os.path.exists(sitemap_path))
            if not os.path.exists(c_index_path):
                c_index_path = index_path

            try:
                if libname == 'adaptivedemux':
                    c_flags = get_c_flags(f'gstreamer-base-{self.project_version}', self.buildroot)
                    c_flags += [f'-I{self.srcdir}/../gst-libs']
                elif libname == 'opencv':
                    c_flags = get_c_flags(f'gstreamer-base-{self.project_version}', self.buildroot)
                    c_flags += get_c_flags(f'gstreamer-video-{self.project_version}', self.buildroot)
                    c_flags += get_c_flags(['opencv4', 'opencv'], self.buildroot, uninstalled=True)
                    c_flags += [f'-I{self.srcdir}/../gst-libs']
                else:
                    c_flags = get_c_flags(f'gstreamer-{libname}-{self.project_version}', self.buildroot)
            except Exception as e:
                print(f'Cannot document {libname}')
                print(e)
                continue

            c_flags += ['-DGST_USE_UNSTABLE_API']

            if libname == 'opencv':
                c_flags += ['-x c++']

            conf = {
                'sitemap': sitemap_path,
                'index': index_path,
                'c_index': c_index_path,
                'output': f'{project_name}-doc',
                'conf_file': str(conf_path),
                'project_name': project_name,
                'project_version': self.project_version,
                'c_smart_index': True,
                'c_order_generated_subpages': True,
                'c_sources': c_sources,
                'include_paths': [
                    os.path.join(self.builddir),
                    os.path.join(self.srcdir),
                ],
                'c_source_filters': [str(s) for s in self.c_source_filters],
                'extra_assets': os.path.join(self.srcdir, 'images'),
                'extra_c_flags': c_flags
            }

            with conf_path.open('w') as f:
                json.dump(conf, f, indent=4)

            conf_files.append(str(conf_path))


        if self.output is not None:
            with self.output.open('w') as f:
                json.dump(conf_files, f, indent=4)

        return conf_files


class GstPluginsHotdocConfGen:
    def __init__(self):

        parser = ArgumentParser()
        parser.add_argument('--builddir', type=P)
        parser.add_argument('--gst_cache_file', type=P)
        parser.add_argument('--sitemap', type=P)
        parser.add_argument('--index', type=P)
        parser.add_argument('--c_flags')
        parser.add_argument('--gst_index', type=P)
        parser.add_argument('--gst_c_sources', nargs='*', default=[])
        parser.add_argument('--project_version')
        parser.add_argument('--include_paths', nargs='*', default=[])
        parser.add_argument('--gst_c_source_filters', nargs='*', default=[])
        parser.add_argument('--gst_c_source_file', type=P)
        parser.add_argument('--gst_plugin_libraries_file', type=P)
        parser.add_argument('--output', type=P)

        parser.parse_args(namespace=self, args=sys.argv[2:])

    def generate_plugins_configs(self):
        plugin_files = []

        if self.gst_c_source_file is not None:
            with self.gst_c_source_file.open() as fd:
                gst_c_source_map = json.load(fd)
        else:
            gst_c_source_map = {}

        if self.gst_plugin_libraries_file is not None:
            with self.gst_plugin_libraries_file.open() as fd:
                gst_plugin_libraries_map = json.load(fd)
        else:
            gst_plugin_libraries_map = {}

        with self.gst_cache_file.open() as fd:
            all_plugins = json.load(fd)

            for plugin_name in all_plugins.keys():
                conf = self.builddir / f'plugin-{plugin_name}.json'
                plugin_files.append(str(conf))

                # New-style, sources are explicitly provided, as opposed to using wildcards
                if plugin_name in gst_c_source_map:
                    gst_c_sources = gst_c_source_map[plugin_name].split(os.pathsep)
                else:
                    gst_c_sources = self.gst_c_sources

                with conf.open('w') as f:
                    json.dump({
                        'sitemap': str(self.sitemap),
                        'index': str(self.index),
                        'gst_index': str(self.index),
                        'output': f'plugin-{plugin_name}',
                        'conf': str(conf),
                        'project_name': plugin_name,
                        'project_version': self.project_version,
                        'gst_cache_file': str(self.gst_cache_file),
                        'gst_plugin_name': plugin_name,
                        'c_flags': self.c_flags,
                        'gst_smart_index': True,
                        'gst_c_sources': gst_c_sources,
                        'gst_c_source_filters': [str(s) for s in self.gst_c_source_filters],
                        'include_paths': self.include_paths,
                        'gst_order_generated_subpages': True,
                        'gst_plugin_library': gst_plugin_libraries_map.get(plugin_name),
                    }, f, indent=4)

        if self.output is not None:
            with self.output.open('w') as f:
                json.dump(plugin_files, f, indent=4)

        return plugin_files


# Marks values in the json file as "unstable" so that they are
# not updated automatically, this aims at making the cache file
# stable and handle corner cases were we can't automatically
# make it happen. For properties, the best way is to use th
# GST_PARAM_DOC_SHOW_DEFAULT flag.
UNSTABLE_VALUE = "unstable-values"


def dict_recursive_update(d, u):
    modified = False
    unstable_values = d.get(UNSTABLE_VALUE, [])
    if not isinstance(unstable_values, list):
        unstable_values = [unstable_values]
    for k, v in u.items():
        if isinstance(v, Mapping):
            r = d.get(k, {})
            modified |= dict_recursive_update(r, v)
            d[k] = r
        elif k not in unstable_values:
            modified = True
            if k == "package":
                d[k] = re.sub(" git$| source release$| prerelease$", "", v)
            else:
                d[k] = u[k]
    return modified


def test_unstable_values():
    current_cache = {"v1": "yes", "unstable-values": "v1"}
    new_cache = {"v1": "no"}

    assert (dict_recursive_update(current_cache, new_cache) is False)

    new_cache = {"v1": "no", "unstable-values": "v2"}
    assert (dict_recursive_update(current_cache, new_cache) is True)

    current_cache = {"v1": "yes", "v2": "yay", "unstable-values": "v1", }
    new_cache = {"v1": "no"}
    assert (dict_recursive_update(current_cache, new_cache) is False)

    current_cache = {"v1": "yes", "v2": "yay", "unstable-values": "v2"}
    new_cache = {"v1": "no", "v2": "unstable"}
    assert (dict_recursive_update(current_cache, new_cache) is True)
    assert (current_cache == {"v1": "no", "v2": "yay", "unstable-values": "v2"})


if __name__ == "__main__":
    if sys.argv[1] == "hotdoc-config":
        fs = GstPluginsHotdocConfGen().generate_plugins_configs()
        print(os.pathsep.join(fs))
        sys.exit(0)
    elif sys.argv[1] == "hotdoc-lib-config":
        fs = GstLibsHotdocConfGen().generate_libs_configs()
        sys.exit(0)

    cache_filename = sys.argv[1]
    output_filename = sys.argv[2]
    build_root = os.environ.get('MESON_BUILD_ROOT', '')

    subenv = os.environ.copy()
    cache = {}
    try:
        with open(cache_filename, newline='\n', encoding='utf8') as f:
            cache = json.load(f)
    except FileNotFoundError:
        pass

    out = output_filename + '.tmp'
    cmd = [os.path.join(os.path.dirname(os.path.realpath(__file__)), 'gst-hotdoc-plugins-scanner'), out]
    gst_plugins_paths = []
    for plugin_path in sys.argv[3:]:
        cmd.append(plugin_path)
        gst_plugins_paths.append(os.path.dirname(plugin_path))

    try:
        with open(os.path.join(build_root, 'GstPluginsPath.json'), newline='\n', encoding='utf8') as f:
            plugin_paths = os.pathsep.join(json.load(f))
    except FileNotFoundError:
        plugin_paths = ""

    if plugin_paths:
        subenv['GST_PLUGIN_PATH'] = subenv.get('GST_PLUGIN_PATH', '') + ':' + plugin_paths

    # Hide stderr unless an actual error happens as we have cases where we get g_warnings
    # and other issues because plugins are being built while `gst_init` is called
    stderrlogfile = output_filename + '.stderr'
    with open(stderrlogfile, 'w', encoding='utf8') as log:
        try:
            data = subprocess.check_output(cmd, env=subenv, stderr=log, encoding='utf8', universal_newlines=True)
        except subprocess.CalledProcessError as e:
            log.flush()
            with open(stderrlogfile, 'r', encoding='utf8') as f:
                print(f.read(), file=sys.stderr, end='')
            raise

    with open(out, 'r', newline='\n', encoding='utf8') as jfile:
        try:
            plugins = json.load(jfile, object_pairs_hook=OrderedDict)
        except json.decoder.JSONDecodeError:
            print("Could not decode:\n%s" % jfile.read(), file=sys.stderr)
            raise

    modified = dict_recursive_update(cache, plugins)

    with open(output_filename, 'w', newline='\n', encoding='utf8') as f:
        json.dump(cache, f, indent=4, sort_keys=True, ensure_ascii=False)

    if modified:
        with open(cache_filename, 'w', newline='\n', encoding='utf8') as f:
            json.dump(cache, f, indent=4, sort_keys=True, ensure_ascii=False)
