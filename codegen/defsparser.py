# -*- Mode: Python; py-indent-offset: 4 -*-
import os, sys
import scmexpr
from definitions import BoxedDef, EnumDef, FlagsDef, FunctionDef, \
     InterfaceDef, MethodDef, ObjectDef, MiniObjectDef, PointerDef, \
     VirtualDef

class IncludeParser(scmexpr.Parser):
    """A simple parser that follows include statements automatically"""
    def include(self, filename):
        if not os.path.isabs(filename):
            filename = os.path.join(os.path.dirname(self.filename), filename)

        # set self.filename to the include name, to handle recursive includes
        oldfile = self.filename
        self.filename = filename
        self.startParsing()
        self.filename = oldfile

class DefsParser(IncludeParser):
    def __init__(self, arg, defines={}):
	IncludeParser.__init__(self, arg)
	self.objects = []
        self.miniobjects = []
        self.interfaces = []
        self.enums = []      # enums and flags
        self.boxes = []      # boxed types
        self.pointers = []   # pointer types
        self.functions = []  # functions and methods
        self.virtuals = []   # virtual methods
        self.c_name = {}     # hash of c names of functions
        self.methods = {}    # hash of methods of particular objects
        self.defines = defines      # -Dfoo=bar options, as dictionary

    def define_object(self, *args):
	odef = apply(ObjectDef, args)
	self.objects.append(odef)
	self.c_name[odef.c_name] = odef
    # TODO: define_mini_object
    def define_miniobject(self, *args):
        odef = apply(MiniObjectDef, args)
        self.miniobjects.append(odef)
        self.c_name[odef.c_name] = odef
    def define_interface(self, *args):
        idef = apply(InterfaceDef, args)
        self.interfaces.append(idef)
        self.c_name[idef.c_name] = idef
    def define_enum(self, *args):
        edef = apply(EnumDef, args)
        self.enums.append(edef)
        self.c_name[edef.c_name] = edef
    def define_flags(self, *args):
        fdef = apply(FlagsDef, args)
        self.enums.append(fdef)
        self.c_name[fdef.c_name] = fdef
    def define_boxed(self, *args):
        bdef = apply(BoxedDef, args)
        self.boxes.append(bdef)
        self.c_name[bdef.c_name] = bdef
    def define_pointer(self, *args):
        pdef = apply(PointerDef, args)
        self.pointers.append(pdef)
        self.c_name[pdef.c_name] = pdef
    def define_function(self, *args):
        fdef = apply(FunctionDef, args)
        self.functions.append(fdef)
        self.c_name[fdef.c_name] = fdef
    def define_method(self, *args):
        mdef = apply(MethodDef, args)
        self.functions.append(mdef)
        self.c_name[mdef.c_name] = mdef
    def define_virtual(self, *args):
        vdef = apply(VirtualDef, args)
        self.virtuals.append(vdef)
    def merge(self, old, parmerge):
        for obj in self.objects:
            if old.c_name.has_key(obj.c_name):
                obj.merge(old.c_name[obj.c_name])
        for f in self.functions:
            if old.c_name.has_key(f.c_name):
                f.merge(old.c_name[f.c_name], parmerge)

    def printMissing(self, old):
        for obj in self.objects:
            if not old.c_name.has_key(obj.c_name):
                obj.write_defs()
        for f in self.functions:
            if not old.c_name.has_key(f.c_name):
                f.write_defs()

    def write_defs(self, fp=sys.stdout):
	for obj in self.objects:
	    obj.write_defs(fp)
        # TODO: Add miniobject
        for obj in self.miniobjects:
            obj.write_defs(fp)
        for enum in self.enums:
            enum.write_defs(fp)
        for boxed in self.boxes:
            boxed.write_defs(fp)
        for pointer in self.pointers:
            pointer.write_defs(fp)
        for func in self.functions:
            func.write_defs(fp)

    def find_object(self, c_name):
        for obj in self.objects:
            if obj.c_name == c_name:
                return obj
        else:
            raise ValueError, 'object not found'

    def find_constructor(self, obj, overrides):
        for func in self.functions:
            if isinstance(func, FunctionDef) and \
               func.is_constructor_of == obj.c_name and \
               not overrides.is_ignored(func.c_name):
                return func

    def find_methods(self, obj):
        objname = obj.c_name
        return filter(lambda func, on=objname: isinstance(func, MethodDef) and
                      func.of_object == on, self.functions)

    def find_virtuals(self, obj):
        objname = obj.c_name
        retval = filter(lambda func, on=objname: isinstance(func, VirtualDef) and
                        func.of_object == on, self.virtuals)
        return retval

    def find_functions(self):
        return filter(lambda func: isinstance(func, FunctionDef) and
                      not func.is_constructor_of, self.functions)

    def ifdef(self, *args):
        if args[0] in self.defines:
            for arg in args[1:]:
                self.handle(arg)

    def ifndef(self, *args):
        if args[0] not in self.defines:
            for arg in args[1:]:
                self.handle(arg)
