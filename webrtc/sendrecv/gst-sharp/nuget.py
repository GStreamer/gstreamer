#!/usr/bin/python3
import argparse
import getpass
import os
import sys
import shutil
import subprocess
from datetime import datetime
from urllib.request import urlretrieve
from zipfile import ZipFile

NUSPEC_TEMPLATE = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2011/08/nuspec.xsd">
  <metadata>
    <id>{package_name}</id>
    <authors>{author}</authors>
    <owners>{owner}</owners>
    <licenseUrl>{license_url}</licenseUrl>
    <projectUrl>{project_url}</projectUrl>
    <iconUrl>{icon_url}</iconUrl>
    <requireLicenseAcceptance>false</requireLicenseAcceptance>
    <description>{description}.</description>
    <copyright>{copyright}</copyright>
    <tags>{tags}</tags>
    <version>{version}</version>
    <dependencies>
{dependencies}  </dependencies>
  </metadata>
  <files>
{files}  </files>
</package>
"""

TARGETS_TEMPLATE = r"""<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Target Name="{package_name}CopyMapConfigs" AfterTargets="AfterBuild">
    <CreateItem Include="$(MSBuildThisFileDirectory)\{frameworkdir}\*.config">
      <Output TaskParameter="Include" ItemName="MapConfigs" />
    </CreateItem>

    <Copy SourceFiles="@(MapConfigs)" DestinationFiles="@(MapConfigs->'$(OutDir)\%(RecursiveDir)%(Filename)%(Extension)')" />
  </Target>
</Project>"""


class Nugetifier:
    def cleanup_args(self):
        self.nugetdir = os.path.join(self.builddir,
                                     self.package_name + 'nupkg')
        self.frameworkdir = 'net45'
        self.nuget_build_dir = os.path.join(
            self.nugetdir, 'build', self.frameworkdir)
        self.nuget_lib_dir = os.path.join(
            self.nugetdir, 'lib', self.frameworkdir)
        self.nuspecfile = os.path.join(
            self.nugetdir, '%s.nuspec' % self.package_name)
        self.nugettargets = os.path.join(
            self.nuget_build_dir, "%s.targets" % self.package_name)
        self.nuget = shutil.which('nuget')
        if not self.nuget:
            print("Could not find the `nuget` tool, install it and retry!")
            return -1

        for d in [self.nugetdir, self.nuget_lib_dir, self.nuget_build_dir]:
            os.makedirs(d, exist_ok=True)
        if not self.description:
            self.description = "%s c# bindings" % self.package_name
        if not self.copyright:
            self.copyright = "Copyright %s" % datetime.now().year
        if not self.tags:
            self.tags = self.package_name

        return 0

    def run(self):
        res = self.cleanup_args()
        if res:
            return res

        self.files = ''

        def add_file(path, target="lib"):
            f = '    <file src="%s" target="%s"/>\n' % (
                path, os.path.join(target, os.path.basename(path)))
            self.files += f

        self.dependencies = ''
        for dependency in self.dependency:
            _id, version = dependency.split(":")
            self.dependencies += '    <dependency id="%s" version="%s" />\n' % (
                _id, version)

        for assembly in self.assembly:
            add_file(assembly, os.path.join('lib', self.frameworkdir))

            for f in [assembly + '.config', assembly[:-3] + 'pdb']:
                if os.path.exists(f):
                    add_file(f, os.path.join('build', self.frameworkdir))

        with open(self.nugettargets, 'w') as _:
            print(TARGETS_TEMPLATE.format(**self.__dict__), file=_)
            add_file(self.nugettargets, 'build')

        with open(self.nuspecfile, 'w') as _:
            print(NUSPEC_TEMPLATE.format(**self.__dict__), file=_)

        subprocess.check_call([self.nuget, 'pack', self.nuspecfile],
                              cwd=self.builddir)


class NugetDownloader:
    def reporthook(self, blocknum, blocksize, totalsize):
        readsofar = blocknum * blocksize
        if totalsize > 0:
            percent = readsofar * 1e2 / totalsize
            s = "\r%5.1f%% %*d / %d" % (
                percent, len(str(totalsize)), readsofar, totalsize)
            sys.stderr.write(s)
            if readsofar >= totalsize:  # near the end
                sys.stderr.write("\n")
        else:  # total size is unknown
            sys.stderr.write("read %d\n" % (readsofar,))

    def run(self):
        url = "https://www.nuget.org/api/v2/package/{nuget_name}/{nuget_version}".format(
            **self.__dict__)
        workdir = os.path.join(self.current_builddir,
                               self.nuget_name, self.nuget_version)
        os.makedirs(workdir, exist_ok=True)

        try:
            with open(os.path.join(workdir, 'linkline'), 'r') as f:
                print(f.read())
            return
        except FileNotFoundError:
            pass
        nugetpath = os.path.join(workdir, self.nuget_name) + '.zip'
        print("Downloading %s into %s" % (url, nugetpath), file=sys.stderr)
        urlretrieve(url, nugetpath, self.reporthook)

        lib_paths = [os.path.join('lib', self.csharp_version), 'lib']
        build_path = os.path.join('build', self.csharp_version)
        dll_path = os.path.join(self.nuget_name, self.nuget_version)
        extract_dir = os.path.join(self.current_builddir, dll_path)
        os.makedirs(extract_dir, exist_ok=True)
        linkline = ''

        print("%s - %s" % (self.builddir, extract_dir), file=sys.stderr)
        configs = []
        dlldir = None
        with ZipFile(nugetpath) as zip:
            for lib_path in lib_paths:
                for f in zip.infolist():
                    if f.filename.startswith(lib_path) or f.filename.startswith(build_path):
                        zip.extract(f, path=extract_dir)
                        if f.filename.endswith('.dll'):
                            fpath = os.path.relpath(os.path.join(extract_dir, f.filename), self.builddir)
                            linkline += ' -r:' + fpath

                            dlldir = os.path.dirname(os.path.join(extract_dir, f.filename))
                        elif f.filename.endswith('.dll.config'):
                            configs.append(os.path.join(extract_dir, f.filename))

                if dlldir:
                    break

        print(dlldir, file=sys.stderr)
        for config in configs:
            print(config, file=sys.stderr)
            print(os.path.join(dlldir, os.path.basename(config)), file=sys.stderr)
            os.rename(config, os.path.join(dlldir, os.path.basename(config)))

        with open(os.path.join(workdir, 'linkline'), 'w') as f:
            print(linkline.strip(), file=f)

        print(linkline.strip())


if __name__ == "__main__":
    if "get" not in sys.argv:
        parser = argparse.ArgumentParser()
        parser.add_argument('--builddir')
        parser.add_argument('--package-name')
        parser.add_argument('--author', default=getpass.getuser())
        parser.add_argument('--owner', default=getpass.getuser())
        parser.add_argument('--native', action='append', default=[])
        parser.add_argument('--assembly', action='append', default=[])
        parser.add_argument('--out')
        parser.add_argument('--description')
        parser.add_argument('--copyright')
        parser.add_argument('--version')
        parser.add_argument('--icon-url', default='')
        parser.add_argument('--project-url', default='')
        parser.add_argument('--license-url', default='')
        parser.add_argument('--tags', default='')
        parser.add_argument('--dependency', default=[], action='append')

        runner = Nugetifier()
    else:
        sys.argv.remove('get')
        parser = argparse.ArgumentParser()
        parser.add_argument('--builddir')
        parser.add_argument('--current-builddir')
        parser.add_argument('--nuget-name')
        parser.add_argument('--nuget-version')
        parser.add_argument('--csharp-version')

        runner = NugetDownloader()

    options = parser.parse_args(namespace=runner)
    exit(runner.run())
