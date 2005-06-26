# -*- Mode: Python; py-indent-offset: 4 -*-
import sys

class Definition:
    def __init__(self, *args):
	"""Create a new defs object of this type.  The arguments are the
	components of the definition"""
	raise RuntimeError, "this is an abstract class"
    def merge(self, old):
	"""Merge in customisations from older version of definition"""
	raise RuntimeError, "this is an abstract class"
    def write_defs(self, fp=sys.stdout):
	"""write out this definition in defs file format"""
	raise RuntimeError, "this is an abstract class"

class ObjectDef(Definition):
    def __init__(self, name, *args):
	self.name = name
	self.module = None
	self.parent = None
	self.c_name = None
        self.typecode = None
	self.fields = []
        self.implements = []
	for arg in args:
	    if type(arg) != type(()) or len(arg) < 2:
		continue
	    if arg[0] == 'in-module':
		self.module = arg[1]
	    elif arg[0] == 'parent':
                self.parent = arg[1]
	    elif arg[0] == 'c-name':
		self.c_name = arg[1]
	    elif arg[0] == 'gtype-id':
		self.typecode = arg[1]
	    elif arg[0] == 'fields':
                for parg in arg[1:]:
                    self.fields.append((parg[0], parg[1]))
            elif arg[0] == 'implements':
                self.implements.append(arg[1])
    def merge(self, old):
	# currently the .h parser doesn't try to work out what fields of
	# an object structure should be public, so we just copy the list
	# from the old version ...
	self.fields = old.fields
        self.implements = old.implements
    def write_defs(self, fp=sys.stdout):
	fp.write('(define-object ' + self.name + '\n')
	if self.module:
	    fp.write('  (in-module "' + self.module + '")\n')
	if self.parent != (None, None):	
	    fp.write('  (parent "' + self.parent + '")\n')
        for interface in self.implements:
            fp.write('  (implements "' + interface + '")\n')
	if self.c_name:
	    fp.write('  (c-name "' + self.c_name + '")\n')
	if self.typecode:
	    fp.write('  (gtype-id "' + self.typecode + '")\n')
        if self.fields:
            fp.write('  (fields\n')
            for (ftype, fname) in self.fields:
                fp.write('    \'("' + ftype + '" "' + fname + '")\n')
            fp.write('  )\n')
	fp.write(')\n\n')

class MiniObjectDef(Definition):
    def __init__(self, name, *args):
	self.name = name
	self.module = None
	self.parent = None
	self.c_name = None
        self.typecode = None
	self.fields = []
        self.implements = []
	for arg in args:
	    if type(arg) != type(()) or len(arg) < 2:
		continue
	    if arg[0] == 'in-module':
		self.module = arg[1]
	    elif arg[0] == 'parent':
                self.parent = arg[1]
	    elif arg[0] == 'c-name':
		self.c_name = arg[1]
	    elif arg[0] == 'gtype-id':
		self.typecode = arg[1]
	    elif arg[0] == 'fields':
                for parg in arg[1:]:
                    self.fields.append((parg[0], parg[1]))
            elif arg[0] == 'implements':
                self.implements.append(arg[1])
    def merge(self, old):
	# currently the .h parser doesn't try to work out what fields of
	# an object structure should be public, so we just copy the list
	# from the old version ...
	self.fields = old.fields
        self.implements = old.implements
    def write_defs(self, fp=sys.stdout):
	fp.write('(define-object ' + self.name + '\n')
	if self.module:
	    fp.write('  (in-module "' + self.module + '")\n')
	if self.parent != (None, None):	
	    fp.write('  (parent "' + self.parent + '")\n')
        for interface in self.implements:
            fp.write('  (implements "' + interface + '")\n')
	if self.c_name:
	    fp.write('  (c-name "' + self.c_name + '")\n')
	if self.typecode:
	    fp.write('  (gtype-id "' + self.typecode + '")\n')
        if self.fields:
            fp.write('  (fields\n')
            for (ftype, fname) in self.fields:
                fp.write('    \'("' + ftype + '" "' + fname + '")\n')
            fp.write('  )\n')
	fp.write(')\n\n')


class InterfaceDef(Definition):
    def __init__(self, name, *args):
	self.name = name
	self.module = None
	self.c_name = None
        self.typecode = None
	self.fields = []
	for arg in args:
	    if type(arg) != type(()) or len(arg) < 2:
		continue
	    if arg[0] == 'in-module':
		self.module = arg[1]
	    elif arg[0] == 'c-name':
		self.c_name = arg[1]
	    elif arg[0] == 'gtype-id':
		self.typecode = arg[1]
    def write_defs(self, fp=sys.stdout):
	fp.write('(define-interface ' + self.name + '\n')
	if self.module:
	    fp.write('  (in-module "' + self.module + '")\n')
	if self.c_name:
	    fp.write('  (c-name "' + self.c_name + '")\n')
	if self.typecode:
	    fp.write('  (gtype-id "' + self.typecode + '")\n')
	fp.write(')\n\n')

class EnumDef(Definition):
    def __init__(self, name, *args):
	self.deftype = 'enum'
	self.name = name
	self.in_module = None
	self.c_name = None
        self.typecode = None
	self.values = []
	for arg in args:
	    if type(arg) != type(()) or len(arg) < 2:
		continue
	    if arg[0] == 'in-module':
		self.in_module = arg[1]
	    elif arg[0] == 'c-name':
		self.c_name = arg[1]
	    elif arg[0] == 'gtype-id':
		self.typecode = arg[1]
	    elif arg[0] == 'values':
                for varg in arg[1:]:
                    self.values.append((varg[0], varg[1]))
    def merge(self, old):
	pass
    def write_defs(self, fp=sys.stdout):
	fp.write('(define-' + self.deftype + ' ' + self.name + '\n')
	if self.in_module:
	    fp.write('  (in-module "' + self.in_module + '")\n')
	fp.write('  (c-name "' + self.c_name + '")\n')
	fp.write('  (gtype-id "' + self.typecode + '")\n')
        if self.values:
            fp.write('  (values\n')
            for name, val in self.values:
                fp.write('    \'("' + name + '" "' + val + '")\n')
            fp.write('  )\n')
	fp.write(')\n\n')

class FlagsDef(EnumDef):
    def __init__(self, *args):
	apply(EnumDef.__init__, (self,) + args)
	self.deftype = 'flags'

class BoxedDef(Definition):
    def __init__(self, name, *args):
	self.name = name
	self.module = None
	self.c_name = None
        self.typecode = None
        self.copy = None
        self.release = None
	self.fields = []
	for arg in args:
	    if type(arg) != type(()) or len(arg) < 2:
		continue
	    if arg[0] == 'in-module':
		self.module = arg[1]
	    elif arg[0] == 'c-name':
		self.c_name = arg[1]
	    elif arg[0] == 'gtype-id':
		self.typecode = arg[1]
            elif arg[0] == 'copy-func':
                self.copy = arg[1]
            elif arg[0] == 'release-func':
                self.release = arg[1]
	    elif arg[0] == 'fields':
                for parg in arg[1:]:
                    self.fields.append((parg[0], parg[1]))
    def merge(self, old):
	# currently the .h parser doesn't try to work out what fields of
	# an object structure should be public, so we just copy the list
	# from the old version ...
	self.fields = old.fields
    def write_defs(self, fp=sys.stdout):
	fp.write('(define-boxed ' + self.name + '\n')
	if self.module:
	    fp.write('  (in-module "' + self.module + '")\n')
	if self.c_name:
	    fp.write('  (c-name "' + self.c_name + '")\n')
	if self.typecode:
	    fp.write('  (gtype-id "' + self.typecode + '")\n')
        if self.copy:
            fp.write('  (copy-func "' + self.copy + '")\n')
        if self.release:
            fp.write('  (release-func "' + self.release + '")\n')
        if self.fields:
            fp.write('  (fields\n')
            for (ftype, fname) in self.fields:
                fp.write('    \'("' + ftype + '" "' + fname + '")\n')
            fp.write('  )\n')
	fp.write(')\n\n')

class PointerDef(Definition):
    def __init__(self, name, *args):
	self.name = name
	self.module = None
	self.c_name = None
        self.typecode = None
	self.fields = []
	for arg in args:
	    if type(arg) != type(()) or len(arg) < 2:
		continue
	    if arg[0] == 'in-module':
		self.module = arg[1]
	    elif arg[0] == 'c-name':
		self.c_name = arg[1]
	    elif arg[0] == 'gtype-id':
		self.typecode = arg[1]
	    elif arg[0] == 'fields':
                for parg in arg[1:]:
                    self.fields.append((parg[0], parg[1]))
    def merge(self, old):
	# currently the .h parser doesn't try to work out what fields of
	# an object structure should be public, so we just copy the list
	# from the old version ...
	self.fields = old.fields
    def write_defs(self, fp=sys.stdout):
	fp.write('(define-pointer ' + self.name + '\n')
	if self.module:
	    fp.write('  (in-module "' + self.module + '")\n')
	if self.c_name:
	    fp.write('  (c-name "' + self.c_name + '")\n')
	if self.typecode:
	    fp.write('  (gtype-id "' + self.typecode + '")\n')
        if self.fields:
            fp.write('  (fields\n')
            for (ftype, fname) in self.fields:
                fp.write('    \'("' + ftype + '" "' + fname + '")\n')
            fp.write('  )\n')
	fp.write(')\n\n')

class MethodDef(Definition):
    def __init__(self, name, *args):
        dump = 0
	self.name = name
	self.ret = None
        self.caller_owns_return = None
	self.c_name = None
        self.typecode = None
	self.of_object = None
	self.params = [] # of form (type, name, default, nullok)
        self.varargs = 0
        self.deprecated = None
	for arg in args:
	    if type(arg) != type(()) or len(arg) < 2:
		continue
	    if arg[0] == 'of-object':
                self.of_object = arg[1]
	    elif arg[0] == 'c-name':
		self.c_name = arg[1]
	    elif arg[0] == 'gtype-id':
		self.typecode = arg[1]
	    elif arg[0] == 'return-type':
		self.ret = arg[1]
            elif arg[0] == 'caller-owns-return':
                self.caller_owns_return = arg[1] in ('t', '#t')
	    elif arg[0] == 'parameters':
                for parg in arg[1:]:
                    ptype = parg[0]
                    pname = parg[1]
                    pdflt = None
                    pnull = 0
                    for farg in parg[2:]:
                        if farg[0] == 'default':
                            pdflt = farg[1]
                        elif farg[0] == 'null-ok':
                            pnull = 1
                    self.params.append((ptype, pname, pdflt, pnull))
            elif arg[0] == 'varargs':
                self.varargs = arg[1] in ('t', '#t')
            elif arg[0] == 'deprecated':
                self.deprecated = arg[1]
            else:
                sys.stderr.write("Warning: %s argument unsupported.\n" 
                                 % (arg[0]))
                dump = 1
        if dump:
            self.write_defs(sys.stderr)

        if self.caller_owns_return is None and self.ret is not None:
            if self.ret[:6] == 'const-':
                self.caller_owns_return = 0
            elif self.ret in ('char*', 'gchar*', 'string'):
                self.caller_owns_return = 1
            else:
                self.caller_owns_return = 0
        for item in ('c_name', 'of_object'):
            if self.__dict__[item] == None:
                self.write_defs(sys.stderr)
                raise RuntimeError, "definition missing required %s" % (item,)
            
    def merge(self, old):
	# here we merge extra parameter flags accross to the new object.
	for i in range(len(self.params)):
	    ptype, pname, pdflt, pnull = self.params[i]
	    for p2 in old.params:
		if p2[1] == pname:
		    self.params[i] = (ptype, pname, p2[2], p2[3])
		    break
    def write_defs(self, fp=sys.stdout):
	fp.write('(define-method ' + self.name + '\n')
	if self.of_object != (None, None):
	    fp.write('  (of-object "' + self.of_object + '")\n')
	if self.c_name:
	    fp.write('  (c-name "' + self.c_name + '")\n')
	if self.typecode:
	    fp.write('  (gtype-id "' + self.typecode + '")\n')
	if self.ret:
	    fp.write('  (return-type "' + self.ret + '")\n')
	if self.deprecated:
	    fp.write('  (deprecated "' + self.deprecated + '")\n')
        if self.params:
            fp.write('  (parameters\n')
            for ptype, pname, pdflt, pnull in self.params:
                fp.write('    \'("' + ptype + '" "' + pname +'"')
                if pdflt: fp.write(' (default "' + pdflt + '")')
                if pnull: fp.write(' (null-ok)')
                fp.write(')\n')
            fp.write('  )\n')
	fp.write(')\n\n')

class FunctionDef(Definition):
    def __init__(self, name, *args):
        dump = 0
	self.name = name
	self.in_module = None
	self.is_constructor_of = None
	self.ret = None
        self.caller_owns_return = None
	self.c_name = None
        self.typecode = None
	self.params = [] # of form (type, name, default, nullok)
        self.varargs = 0
        self.deprecated = None
	for arg in args:
	    if type(arg) != type(()) or len(arg) < 2:
		continue
	    if arg[0] == 'in-module':
		self.in_module = arg[1]
	    elif arg[0] == 'is-constructor-of':
		self.is_constructor_of = arg[1]
	    elif arg[0] == 'c-name':
		self.c_name = arg[1]
	    elif arg[0] == 'gtype-id':
		self.typecode = arg[1]
	    elif arg[0] == 'return-type':
		self.ret = arg[1]
            elif arg[0] == 'caller-owns-return':
                self.caller_owns_return = arg[1] in ('t', '#t')
	    elif arg[0] == 'parameters':
                for parg in arg[1:]:
                    ptype = parg[0]
                    pname = parg[1]
                    pdflt = None
                    pnull = 0
                    for farg in parg[2:]:
                        if farg[0] == 'default':
                            pdflt = farg[1]
                        elif farg[0] == 'null-ok':
                            pnull = 1
                    self.params.append((ptype, pname, pdflt, pnull))
            elif arg[0] == 'varargs':
                self.varargs = arg[1] in ('t', '#t')
            elif arg[0] == 'deprecated':
                self.deprecated = arg[1]
            else:
                sys.stderr.write("Warning: %s argument unsupported\n"
                                 % (arg[0],))
                dump = 1
        if dump:
            self.write_defs(sys.stderr)

        if self.caller_owns_return is None and self.ret is not None:
            if self.ret[:6] == 'const-':
                self.caller_owns_return = 0
            elif self.is_constructor_of:
                self.caller_owns_return = 1
            elif self.ret in ('char*', 'gchar*', 'string'):
                self.caller_owns_return = 1
            else:
                self.caller_owns_return = 0
        for item in ('c_name',):
            if self.__dict__[item] == None:
                self.write_defs(sys.stderr)
                raise RuntimeError, "definition missing required %s" % (item,)

    _method_write_defs = MethodDef.__dict__['write_defs']

    def merge(self, old):
	# here we merge extra parameter flags accross to the new object.
	for i in range(len(self.params)):
	    ptype, pname, pdflt, pnull = self.params[i]
	    for p2 in old.params:
		if p2[1] == pname:
		    self.params[i] = (ptype, pname, p2[2], p2[3])
		    break
	if not self.is_constructor_of:
            try:
                self.is_constructor_of = old.is_constructor_of
            except AttributeError:
                pass
	if isinstance(old, MethodDef):
	    self.name = old.name
	    # transmogrify from function into method ...
	    self.write_defs = self._method_write_defs
	    self.of_object = old.of_object
	    del self.params[0]
    def write_defs(self, fp=sys.stdout):
	fp.write('(define-function ' + self.name + '\n')
	if self.in_module:
	    fp.write('  (in-module "' + self.in_module + '")\n')
	if self.is_constructor_of:
	    fp.write('  (is-constructor-of "' + self.is_constructor_of +'")\n')
	if self.c_name:
	    fp.write('  (c-name "' + self.c_name + '")\n')
	if self.typecode:
	    fp.write('  (gtype-id "' + self.typecode + '")\n')
	if self.ret:
	    fp.write('  (return-type "' + self.ret + '")\n')
	if self.deprecated:
	    fp.write('  (deprecated "' + self.deprecated + '")\n')
        if self.params:
            fp.write('  (parameters\n')
            for ptype, pname, pdflt, pnull in self.params:
                fp.write('    \'("' + ptype + '" "' + pname +'"')
                if pdflt: fp.write(' (default "' + pdflt + '")')
                if pnull: fp.write(' (null-ok)')
                fp.write(')\n')
            fp.write('  )\n')
	fp.write(')\n\n')

