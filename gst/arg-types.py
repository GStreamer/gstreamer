# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python
# Copyright (C) 2002 David I. Lehn
#               2004 Johan Dahlin
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
# Author: David I. Lehn <dlehn@users.sourceforge.net>

from argtypes import UInt64Arg, Int64Arg, PointerArg, ArgMatcher, ArgType, matcher

class GstDataPtrArg(ArgType):
    normal = ('    if (!pygst_data_from_pyobject(py_%(name)s, &%(name)s))\n'
              '        return NULL;\n')
    null =   ('    if (py_%(name)s == Py_None)\n'
              '        %(name)s = NULL;\n'
              '    else if (pyst_data_from_pyobject(py_%(name)s, %(name)s_rect))\n'
              '        %(name)s = &%(name)s_rect;\n'
              '    else\n'
              '            return NULL;\n')
    def write_param(self, ptype, pname, pdflt, pnull, info):
        if pnull:
            info.varlist.add('GstData', pname + '_data')
            info.varlist.add('GstData', '*' + pname)
            info.varlist.add('PyObject', '*py_' + pname + ' = Py_None')
            info.add_parselist('O', ['&py_' + pname], [pname])
            info.arglist.append(pname)
            info.codebefore.append(self.null % {'name':  pname})
        else:
            info.varlist.add('GstData*', pname)
            info.varlist.add('PyObject', '*py_' + pname)
            info.add_parselist('O', ['&py_' + pname], [pname])
            info.arglist.append(pname)
            info.codebefore.append(self.normal % {'name':  pname})

arg = GstDataPtrArg()
matcher.register('GstData*', arg)
matcher.register('GstClockTime', UInt64Arg())
matcher.register('GstClockTimeDiff', Int64Arg())

arg = PointerArg('gpointer', 'G_TYPE_POINTER')
matcher.register('GstClockID', arg)

del arg
