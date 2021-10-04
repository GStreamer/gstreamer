#!/usr/bin/env python3

"""
Copies current subproject git repository to create a cache
"""

import shutil
import os
import sys
import argparse

DEST = "/subprojects"
PARSER = argparse.ArgumentParser()
PARSER.add_argument('subprojects_dir')
PARSER.add_argument('--build', action="store_true", default=False)


def create_cache_in_image(options):
    os.makedirs(DEST, exist_ok=True)
    print("Creating cache from %s" % options.subprojects_dir)
    for project_name in os.listdir(options.subprojects_dir):
        project_path = os.path.join(options.subprojects_dir, project_name)

        if project_name != "packagecache" and not os.path.exists(os.path.join(project_path, '.git')):
            continue

        print("Copying %s" % project_name)
        shutil.copytree(project_path, os.path.join(DEST, project_name))


def copy_cache(options):
    # FIXME Remove when not needed anymore.
    for path in [DEST, "/gst-build/subprojects", r"C:\gst-build\subprojects"]:
        if not os.path.exists(path):
            print("%s doesn't exist." % path)
            continue

        for project_name in os.listdir(path):
            project_path = os.path.join(options.subprojects_dir, project_name)

            cache_dir = os.path.join(path, project_name)
            if os.path.exists(project_path):
                print("- Ignoring %s" % cache_dir)
                continue

            if not os.path.isdir(cache_dir):
                print("- Ignoring %s" % cache_dir)
                continue

            print("Copying from %s" % cache_dir)
            shutil.copytree(cache_dir, project_path)


if __name__ == "__main__":
    options = PARSER.parse_args()

    if options.build:
        create_cache_in_image(options)
    else:
        copy_cache(options)
