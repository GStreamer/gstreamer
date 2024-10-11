#!/usr/bin/env python3

"""
Copies current subproject git repository to create a cache
"""

import shutil
import os
import argparse
import subprocess

PARSER = argparse.ArgumentParser()
PARSER.add_argument('subprojects_dir')
PARSER.add_argument('--build', action="store_true", default=False)
PARSER.add_argument('--cache-dir', default="/subprojects")


def create_cache_in_image(options):
    os.makedirs(options.cache_dir, exist_ok=True)
    print("Creating cache from %s" % options.subprojects_dir)
    for project_name in os.listdir(options.subprojects_dir):
        project_path = os.path.join(options.subprojects_dir, project_name)

        if project_name != "packagecache" and not os.path.exists(os.path.join(project_path, '.git')):
            continue

        if os.path.exists(os.path.join(options.cache_dir, project_name)):
            continue

        print("Copying %s" % project_name)
        shutil.copytree(project_path, os.path.join(options.cache_dir, project_name))

    media_path = os.path.join(options.subprojects_dir, '..', '.git',
        'modules', 'subprojects', 'gst-integration-testsuites', 'medias')
    if os.path.exists(os.path.join(options.cache_dir, 'medias.git')):
        return

    if os.path.exists(media_path):
        print("Creating media cache")
        shutil.copytree(media_path, os.path.join(options.cache_dir, 'medias.git'))
    else:
        print("Did not find medias in %s" % media_path)


def copy_cache(options):
    for path in [options.cache_dir]:
        if not os.path.exists(path):
            print("%s doesn't exist." % path)
            continue

        for project_name in os.listdir(path):
            project_path = os.path.join(options.subprojects_dir, project_name)
            cache_dir = os.path.join(path, project_name)

            if project_name == 'medias.git':
                project_path = os.path.join(options.subprojects_dir, '..', '.git', 'modules',
                    'subprojects', 'gst-integration-testsuites')
                os.makedirs(project_path, exist_ok=True)
                project_path = os.path.join(project_path, 'medias')

            if os.path.exists(project_path):
                print("- Ignoring %s" % cache_dir)
                continue

            if not os.path.isdir(cache_dir):
                print("- Ignoring %s" % cache_dir)
                continue

            print("Copying from %s -> %s" % (cache_dir, project_path))
            shutil.copytree(cache_dir, project_path)
    subprocess.check_call(['meson', 'subprojects', 'update', '--reset'])


def upgrade_meson():
    # MESON_COMMIT variable can be set when creating a pipeline to test meson pre releases
    meson_commit = os.environ.get('MESON_COMMIT')
    if meson_commit:
        url = f'git+https://github.com/mesonbuild/meson.git@{meson_commit}'
        subprocess.check_call(['pip3', 'install', '--upgrade', url])


if __name__ == "__main__":
    options = PARSER.parse_args()

    if options.build:
        create_cache_in_image(options)
    else:
        upgrade_meson()
        copy_cache(options)
