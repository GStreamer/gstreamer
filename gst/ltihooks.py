# -*- Mode: Python; py-indent-offset: 4 -*-
# ltihooks.py: python import hooks that understand libtool libraries.
# Copyright (C) 2000 James Henstridge.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

import os, ihooks

class LibtoolHooks(ihooks.Hooks):
    def get_suffixes(self):
        """Like normal get_suffixes, but adds .la suffixes to list"""
	ret = ihooks.Hooks.get_suffixes(self)
	ret.insert(0, ('module.la', 'rb', 3))
	ret.insert(0, ('.la', 'rb', 3))
	return ret

    def load_dynamic(self, name, filename, file=None):
        """Like normal load_dynamic, but treat .la files specially"""
	if len(filename) > 3 and filename[-3:] == '.la':
	    fp = open(filename, 'r')
	    dlname = ''
	    installed = 1
	    line = fp.readline()
	    while line:
		if len(line) > 7 and line[:7] == 'dlname=':
		    dlname = line[8:-2]
		elif len(line) > 10 and line[:10] == 'installed=':
		    installed = line[10:-1] == 'yes'
		line = fp.readline()
	    fp.close()
	    if dlname:
		if installed:
		    filename = os.path.join(os.path.dirname(filename),
					    dlname)
		else:
		    filename = os.path.join(os.path.dirname(filename),
					    '.libs', dlname)
	return ihooks.Hooks.load_dynamic(self, name, filename, file)

importer = ihooks.ModuleImporter()
importer.set_hooks(LibtoolHooks())

def install():
    importer.install()
def uninstall():
    importer.uninstall()

install()
