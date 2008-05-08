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
from reversewrapper import Parameter, ReturnType, GBoxedParam, GBoxedReturn, IntParam, IntReturn

class XmlNodeArg(ArgType):
	"""libxml2 node generator"""

	names = {"xobj":"xmlNode",
			"xptr":"xmlNodePtr",
			"xwrap":"libxml_xmlNodePtrWrap"}

	parm = ('    if(xml == NULL) return NULL;\n'
			'    xobj = PyObject_GetAttrString(xml, "%(xobj)s");\n'
			'    if(!PyObject_IsInstance(py%(name)s, xobj)) {\n'
			'        PyErr_Clear();\n'
			'        PyErr_SetString(PyExc_RuntimeError,"%(name)s is not a %(xobj)s instance");\n'
			'        Py_DECREF(xobj);Py_DECREF(xml);\n'
			'        return NULL;\n'
			'    }\n'
			'    o = PyObject_GetAttrString(py%(name)s, "_o");\n'
			'    %(name)s = PyCObject_AsVoidPtr(o);\n')
	parmp = ('    Py_DECREF(o); Py_DECREF(xobj);Py_DECREF(xml);\n')

	ret =  ('    if(xml == NULL) return NULL;\n')
	retp = ('    xargs = PyTuple_New(1);\n'
			'    xobj = PyObject_GetAttrString(xml, "%(xobj)s");\n'
			'    o = %(xwrap)s(ret);\n'
			'    PyTuple_SetItem(xargs, 0, o);\n'
			'    return PyInstance_New(xobj, xargs, PyDict_New());\n')

	def write_param(self, ptype, pname, pdflt, pnull, keeprefcount, info):
		info.varlist.add('PyObject', '*xml = _gst_get_libxml2_module()')
		info.varlist.add('PyObject', '*o')
		info.varlist.add('PyObject', '*xobj')
		info.varlist.add('PyObject', '*py' + pname)
		info.varlist.add(self.names["xptr"], pname)
		#if pnull:
		info.add_parselist('O', ['&py'+pname], [pname])
		info.arglist.append(pname)
		self.names["name"] = pname
		info.codebefore.append(self.parm % self.names)
		info.codeafter.append(self.parmp % self.names);
	def write_return(self, ptype, ownsreturn, info):
		info.varlist.add('PyObject', '*xml = _gst_get_libxml2_module()')
		info.varlist.add('PyObject', '*xargs')
		info.varlist.add('PyObject', '*xobj')
		info.varlist.add('PyObject', '*o')
		info.varlist.add(self.names["xptr"], 'ret')
		info.codebefore.append(self.ret % self.names)
		info.codeafter.append(self.retp % self.names)

class XmlDocArg(XmlNodeArg):
	"""libxml2 doc generator"""
	names = {"xobj":"xmlDoc",
			"xptr":"xmlDocPtr",
			"xwrap":"libxml_xmlDocPtrWrap"}

class GstCapsArg(ArgType):
	"""GstCaps node generator"""

	before = ('    %(name)s = pygst_caps_from_pyobject (py_%(name)s, %(namecopy)s);\n'
		  '    if (PyErr_Occurred())\n'
		  '      return NULL;\n')
	beforenull = ('    if (py_%(name)s == Py_None || py_%(name)s == NULL)\n'
		      '        %(name)s = NULL;\n'
		      '    else\n'
		      '  ' + before)
	after = ('    if (%(name)s && %(name)s_is_copy)\n'
		 '        gst_caps_unref (%(name)s);\n')

	def write_param(self, ptype, pname, pdflt, pnull, keeprefcount, info):
		if ptype == 'const-GstCaps*':
			self.write_const_param(pname, pdflt, pnull, info)
		elif ptype == 'GstCaps*':
			self.write_normal_param(pname, pdflt, pnull, info)
		else:
			raise RuntimeError, "write_param not implemented for %s" % ptype

	def write_const_param(self, pname, pdflt, pnull, info):
		if pdflt:
			assert pdflt == 'NULL'
			info.varlist.add('PyObject', '*py_' + pname + ' = NULL')
		else:
			info.varlist.add('PyObject', '*py_' + pname)
		info.varlist.add('GstCaps', '*'+pname)
		info.varlist.add('gboolean', pname+'_is_copy')
		info.add_parselist('O', ['&py_'+pname], [pname])
		info.arglist.append(pname)
		if pnull:
			info.codebefore.append (self.beforenull % { 'name' : pname, 'namecopy' : '&'+pname+'_is_copy' })
		else:
			info.codebefore.append (self.before % { 'name' : pname, 'namecopy' : '&'+pname+'_is_copy' })
		info.codeafter.append (self.after % { 'name' : pname, 'namecopy' : '&'+pname+'_is_copy' })

	def write_normal_param(self, pname, pdflt, pnull, info):
		if pdflt:
			assert pdflt == 'NULL'
			info.varlist.add('PyObject', '*py_' + pname + ' = NULL')
		else:
			info.varlist.add('PyObject', '*py_' + pname)
		info.varlist.add('GstCaps', '*'+pname)
		info.add_parselist('O', ['&py_'+pname], [pname])
		info.arglist.append(pname)
		if pnull:
			info.codebefore.append (self.beforenull % { 'name' : pname, 'namecopy' : 'NULL' })
		else:
			info.codebefore.append (self.before % { 'name' : pname, 'namecopy' : 'NULL' })

	def write_return(self, ptype, ownsreturn, info):
		if ptype == 'GstCaps*':
			info.varlist.add('GstCaps', '*ret')
			copyval = 'FALSE'
		elif ptype == 'const-GstCaps*':
			info.varlist.add('const GstCaps', '*ret')
			copyval = 'TRUE'
		else:
			raise RuntimeError, "write_return not implemented for %s" % ptype
		info.codeafter.append('    return pyg_boxed_new (GST_TYPE_CAPS, ret, '+copyval+', TRUE);')

class GstIteratorArg(ArgType):
	def write_return(self, ptype, ownsreturn, info):
		info.varlist.add('GstIterator', '*ret')
		info.codeafter.append('    return pygst_iterator_new(ret);')

class GstMiniObjectParam(Parameter):

	def get_c_type(self):
		return self.props.get('c_type', 'GstMiniObject *')

	def convert_c2py(self):
		self.wrapper.add_declaration("PyObject *py_%s = NULL;" % self.name)
		self.wrapper.write_code(code=("if (%s) {\n"
					      "    py_%s = pygstminiobject_new((GstMiniObject *) %s);\n"
					      "    gst_mini_object_unref ((GstMiniObject *) %s);\n"
					      "} else {\n"
					      "    Py_INCREF(Py_None);\n"
					      "    py_%s = Py_None;\n"
					      "}"
					      % (self.name, self.name, self.name, self.name, self.name)),
					cleanup=("gst_mini_object_ref ((GstMiniObject *) %s);\nPy_DECREF(py_%s);" % (self.name, self.name)))
		self.wrapper.add_pyargv_item("py_%s" % self.name)

matcher.register_reverse('GstMiniObject*', GstMiniObjectParam)

class GstMiniObjectReturn(ReturnType):

	def get_c_type(self):
		return self.props.get('c_type', 'GstMiniObject *')

	def write_decl(self):
		self.wrapper.add_declaration("%s retval;" % self.get_c_type())

	def write_error_return(self):
		self.wrapper.write_code("return NULL;")

	def write_conversion(self):
		self.wrapper.write_code("retval = (%s) pygstminiobject_get(py_retval);"
					% self.get_c_type())
		self.wrapper.write_code("gst_mini_object_ref((GstMiniObject *) retval);")

matcher.register_reverse_ret('GstMiniObject*', GstMiniObjectReturn)

class GstCapsParam(Parameter):

	def get_c_type(self):
		return self.props.get('c_type', 'GstCaps *')

	def convert_c2py(self):
		self.wrapper.add_declaration("PyObject *py_%s = NULL;" % self.name)
		self.wrapper.write_code(code=("if (%s)\n"
					      "    py_%s = pyg_boxed_new (GST_TYPE_CAPS, %s, FALSE, TRUE);\n"
					      "else {\n"
					      "    Py_INCREF(Py_None);\n"
					      "    py_%s = Py_None;\n"
					      "}"
					      % (self.name, self.name, self.name, self.name)),
					cleanup=("gst_caps_ref(%s);\nPy_DECREF(py_%s);" % (self.name, self.name)))
		self.wrapper.add_pyargv_item("py_%s" % self.name)

matcher.register_reverse('GstCaps*', GstCapsParam)

class GstCapsReturn(ReturnType):

	def get_c_type(self):
		return self.props.get('c_type', 'GstCaps *')

	def write_decl(self):
		self.wrapper.add_declaration("%s retval;" % self.get_c_type())

	def write_error_return(self):
		self.wrapper.write_code("return NULL;")

	def write_conversion(self):
		self.wrapper.write_code("retval = (%s) pygst_caps_from_pyobject (py_retval, NULL);"
					% self.get_c_type())
##         self.wrapper.write_code("gst_mini_object_ref((GstMiniObject *) retval);")

matcher.register_reverse_ret('GstCaps*', GstCapsReturn)


class Int64Param(Parameter):

	def get_c_type(self):
		return self.props.get('c_type', 'gint64')

	def convert_c2py(self):
		self.wrapper.add_declaration("PyObject *py_%s;" % self.name)
		self.wrapper.write_code(code=("py_%s = PyLong_FromLongLong(%s);" %
					      (self.name, self.name)),
					cleanup=("Py_DECREF(py_%s);" % self.name))
		self.wrapper.add_pyargv_item("py_%s" % self.name)

class Int64Return(ReturnType):
	def get_c_type(self):
		return self.props.get('c_type', 'gint64')
	def write_decl(self):
		self.wrapper.add_declaration("%s retval;" % self.get_c_type())
	def write_error_return(self):
		self.wrapper.write_code("return -G_MAXINT;")
	def write_conversion(self):
		self.wrapper.write_code(
		    code=None,
		    failure_expression="!PyLong_Check(py_retval)",
		    failure_cleanup='PyErr_SetString(PyExc_TypeError, "retval should be an long");')
		self.wrapper.write_code("retval = PyLong_AsLongLong(py_retval);")

class UInt64Param(Parameter):

	def get_c_type(self):
		return self.props.get('c_type', 'guint64')

	def convert_c2py(self):
		self.wrapper.add_declaration("PyObject *py_%s;" % self.name)
		self.wrapper.write_code(code=("py_%s = PyLong_FromUnsignedLongLong(%s);" %
					      (self.name, self.name)),
					cleanup=("Py_DECREF(py_%s);" % self.name))
		self.wrapper.add_pyargv_item("py_%s" % self.name)

class UInt64Return(ReturnType):
	def get_c_type(self):
		return self.props.get('c_type', 'guint64')
	def write_decl(self):
		self.wrapper.add_declaration("%s retval;" % self.get_c_type())
	def write_error_return(self):
		self.wrapper.write_code("return -G_MAXINT;")
	def write_conversion(self):
		self.wrapper.write_code(
		    code=None,
		    failure_expression="!PyLong_Check(py_retval)",
		    failure_cleanup='PyErr_SetString(PyExc_TypeError, "retval should be an long");')
		self.wrapper.write_code("retval = PyLong_AsUnsignedLongLongMask(py_retval);")

class ULongParam(Parameter):

	def get_c_type(self):
		return self.props.get('c_type', 'gulong')

	def convert_c2py(self):
		self.wrapper.add_declaration("PyObject *py_%s;" % self.name)
		self.wrapper.write_code(code=("py_%s = PyLong_FromUnsignedLong(%s);" %
					      (self.name, self.name)),
					cleanup=("Py_DECREF(py_%s);" % self.name))
		self.wrapper.add_pyargv_item("py_%s" % self.name)

class ULongReturn(ReturnType):
	def get_c_type(self):
		return self.props.get('c_type', 'gulong')
	def write_decl(self):
		self.wrapper.add_declaration("%s retval;" % self.get_c_type())
	def write_error_return(self):
		self.wrapper.write_code("return -G_MAXINT;")
	def write_conversion(self):
		self.wrapper.write_code(
		    code=None,
		    failure_expression="!PyLong_Check(py_retval)",
		    failure_cleanup='PyErr_SetString(PyExc_TypeError, "retval should be an long");')
		self.wrapper.write_code("retval = PyLong_AsUnsignedLongMask(py_retval);")

class ConstStringReturn(ReturnType):

	def get_c_type(self):
		return "const gchar *"

	def write_decl(self):
		self.wrapper.add_declaration("const gchar *retval;")

	def write_error_return(self):
		self.wrapper.write_code("return NULL;")

	def write_conversion(self):
		self.wrapper.write_code(
		    code=None,
		    failure_expression="!PyString_Check(py_retval)",
		    failure_cleanup='PyErr_SetString(PyExc_TypeError, "retval should be a string");')
		self.wrapper.write_code("retval = g_strdup(PyString_AsString(py_retval));")

class StringArrayArg(ArgType):
	"""Arg type for NULL-terminated string pointer arrays (GStrv, aka gchar**)."""
	def write_return(self, ptype, ownsreturn, info):
		if ownsreturn:
			raise NotImplementedError ()
		else:
			info.varlist.add("gchar", "**ret")
			info.codeafter.append("    if (ret) {\n"
					      "        guint size = g_strv_length(ret);\n"
					      "        PyObject *py_ret = PyTuple_New(size);\n"
					      "        gint i;\n"
					      "        for (i = 0; i < size; i++)\n"
					      "            PyTuple_SetItem(py_ret, i,\n"
					      "                PyString_FromString(ret[i]));\n"
					      "        return py_ret;\n"
					      "    }\n"
					      "    return PyTuple_New (0);\n")

matcher.register('GstClockTime', UInt64Arg())
matcher.register('GstClockTimeDiff', Int64Arg())
matcher.register('xmlNodePtr', XmlNodeArg())
matcher.register('xmlDocPtr', XmlDocArg())
matcher.register('GstCaps', GstCapsArg()) #FIXME: does this work?
matcher.register('GstCaps*', GstCapsArg()) #FIXME: does this work?
matcher.register('const-GstCaps*', GstCapsArg())
matcher.register('GstIterator*', GstIteratorArg())

arg = PointerArg('gpointer', 'G_TYPE_POINTER')
matcher.register('GstClockID', arg)

for typename in ["GstPlugin", "GstStructure", "GstTagList", "GError", "GstDate", "GstSegment"]:
	matcher.register_reverse(typename, GBoxedParam)
	matcher.register_reverse_ret(typename, GBoxedReturn)

for typename in ["GstBuffer*", "GstEvent*", "GstMessage*", "GstQuery*"]:
	matcher.register_reverse(typename, GstMiniObjectParam)
	matcher.register_reverse_ret(typename, GstMiniObjectReturn)

for typename in ["gint64", "GstClockTimeDiff"]:
	matcher.register_reverse(typename, Int64Param)
	matcher.register_reverse_ret(typename, Int64Return)

for typename in ["guint64", "GstClockTime"]:
	matcher.register_reverse(typename, UInt64Param)
	matcher.register_reverse_ret(typename, UInt64Return)

matcher.register_reverse_ret("const-gchar*", ConstStringReturn)

matcher.register_reverse("GType", IntParam)
matcher.register_reverse_ret("GType", IntReturn)

matcher.register_reverse("gulong", ULongParam)
matcher.register_reverse_ret("gulong", ULongReturn)

matcher.register("GStrv", StringArrayArg())

del arg
