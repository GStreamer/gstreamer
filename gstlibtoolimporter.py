# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python
# Copyright (C) 2009 Alessandro Decina <alessandro.decina@collbora.co.uk>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.
#
# Author: Alessandro Decina <alessandro.decina@collabora.co.uk>

# importer for uninstalled setup, see PEP
# http://www.python.org/dev/peps/pep-0302/

import os
import sys
import imp

class Loader(object):
    def __init__(self, fileobj, filename, description):
        self.fileobj = fileobj
        self.filename = filename
        self.description = description

    def find_real_filename(self):
        dlname = None
        installed = False
        for line in self.fileobj:
            if len(line) > 7 and line[:7] == 'dlname=':
                dlname = line[8:-2]
            elif len(line) > 10 and line[:10] == 'installed=':
                installed = line[10:-1] == 'yes'

        if not dlname:
            return None

        if installed or os.path.dirname(self.filename).endswith('.libs'):
            filename = os.path.join(os.path.dirname(self.filename), dlname)
        else:
            filename = os.path.join(os.path.dirname(self.filename), '.libs', dlname)

        return filename

    def load_module(self, name):
        try:
            module = sys.modules[name]
            self.fileobj.close()

            return module
        except KeyError:
            pass

        filename = self.find_real_filename()
        self.fileobj.close()
        if filename is None:
            raise ImportError("No module named %s" % name)

        fileobj = file(filename, 'rb')

        module = imp.new_module(name)
        sys.modules[name] = module

        imp.load_module(name, fileobj, filename, self.description)
        fileobj.close()

        return module

class Importer(object):
    def find_module(self, name, path=None):
        if path is None:
            path = sys.path

        for directory in path:
            fileobj, filename, description = self.find_libtool_module(name, directory)
            if fileobj is not None:
                return Loader(fileobj, filename, description)

        return None

    def find_libtool_module(self, name, directory):
        name = name.split(".")[-1]
        absname = os.path.join(directory, name)
        for suffix in ('.la', '.module.la'):
            filename = absname + suffix
            try:
                fileobj = file(filename, 'rb')
            except IOError:
                continue

            return fileobj, filename, (suffix, 'rb', imp.C_EXTENSION)

        return None, None, None

importer = Importer()

def install():
    sys.meta_path.append(importer)

def uninstall():
    sys.meta_path.remove(importer)
