#!/usr/bin/env python3

import argparse
import os
import subprocess

from collections import namedtuple
import xml.etree.ElementTree as ET

# Disallow git prompting for a username/password
os.environ['GIT_TERMINAL_PROMPT'] = '0'
def git(*args, repository_path='.'):
    return subprocess.check_output(["git"] + list(args), cwd=repository_path).decode()

class Manifest(object):
    '''
    Parse and store the content of a manifest file
    '''

    remotes = {}
    projects = {}
    default_remote = 'origin'
    default_revision = 'refs/heads/master'

    def __init__(self, manifest_path):
        self.manifest_path = manifest_path

    def parse(self):
        try:
            tree = ET.parse(self.manifest_path)
        except Exception as ex:
            raise Exception("Error loading manifest %s in file %s" % (self.manifest_path, ex))

        root = tree.getroot()

        for child in root:
            if child.tag == 'remote':
                self.remotes[child.attrib['name']] = child.attrib['fetch']
            if child.tag == 'default':
                self.default_remote = child.attrib['remote'] or self.default_remote
                self.default_revision = child.attrib['revision'] or self.default_revision
            if child.tag == 'project':
                project = namedtuple('Project', ['name', 'remote',
                    'revision', 'fetch_uri'])

                project.name = child.attrib['name']
                if project.name.endswith('.git'):
                    project.name = project.name[:-4]
                project.remote = child.attrib.get('remote') or self.default_remote
                project.revision = child.attrib.get('revision') or self.default_revision
                project.fetch_uri = self.remotes[project.remote] + project.name + '.git'

                self.projects[project.name] = project

    def find_project(self, name):
        try:
            return self.projects[name]
        except KeyError as ex:
            raise Exception("Could not find project %s in manifest %s" % (name, self.manifest_path))

    def get_fetch_uri(self, project, remote):
        fetch = self.remotes[remote]
        return fetch + project.name + '.git'

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", action="store", type=str)
    parser.add_argument("--destination", action="store", type=str, default='.')
    parser.add_argument("--manifest", action="store", type=str)
    parser.add_argument("--fetch", action="store_true", default=False)
    options = parser.parse_args()

    if not options.project:
        raise ValueError("--project argument not provided")
    if not options.manifest:
        raise ValueError("--manifest argument not provided")

    manifest = Manifest(options.manifest)
    manifest.parse()
    project = manifest.find_project(options.project)

    dest = options.destination
    if dest == '.':
        dest = os.path.join (os.getcwd(), project.name)

    if options.fetch:
        assert os.path.exists(dest) == True
        git('fetch', project.fetch_uri, project.revision, repository_path=dest)
    else:
        git('clone', project.fetch_uri, dest)

    git('checkout', '--detach', project.revision, repository_path=dest)
