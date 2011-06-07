#!/usr/bin/env python
# -*- Mode: Python; py-indent-offset: 4 -*-
# GPL'ed
# Toby D. Reeves <toby@max.rl.plh.af.mil>
#
# Modified by James Henstridge <james@daa.com.au> to output stuff in
# Havoc's new defs format.  Info on this format can be seen at:
#   http://mail.gnome.org/archives/gtk-devel-list/2000-January/msg00070.html
# Updated to be PEP-8 compatible and refactored to use OOP
#
# Scan the given public .h files of a GTK module (or module using
# GTK object conventions) and generates a set of scheme defs.
#
# h2def searches through a header file looking for function prototypes and
# generates a scheme style defenition for each prototype.
# Basically the operation of h2def is:
#
# - read each .h file into a buffer which is scrubbed of extraneous data
# - find all object defenitions:
#   - find all structures that may represent a GtkObject
#   - find all structures that might represent a class
#   - find all structures that may represent a GtkObject subclass
#   - find all structures that might represent a class/Iface inherited from
#     GTypeInterface
# - find all enum defenitions
# - write out the defs
#
# The command line options are:
#
#   -s --separate    Create separate files for objects and function/method defs
#                    using the given name as the base name (optional). If this
#                    is not specified the combined object and function defs
#                    will be output to sys.stdout.
#   -f --defsfilter  Extract defs from the given file to filter the output defs
#                    that is don't output defs that are defined in the
#                    defsfile. More than one deffile may be specified.
#   -m --modulename  The prefix to be stripped from the front of function names
#                    for the given module
#   -n --namespace   The module or namespace name to be used, for example
#                    WebKit where h2def is unable to detect the module name
#                    automatically. it also sets the gtype-id prefix.
#   --onlyenums      Only produce defs for enums and flags
#   --onlyobjdefs    Only produce defs for objects
#   -v               Verbose output
#
# Examples:
#
# python h2def.py /usr/local/include/pango-1.0/pango/*.h >/tmp/pango.defs
#
# - Outputs all defs for the pango module.
#
# python h2def.py -m gdk -s /tmp/gdk-2.10 \
#            -f /usr/tmp/pygtk/gtk/gdk-base.defs \
#            /usr/local/include/gtk-2.0/gdk/*.h \
#            /usr/local/include/gtk-2.0/gdk-pixbuf/*.h
#
# - Outputs the gdk module defs that are not contained in the defs file
#   /usr/tmp/pygtk/gtk/gdk-base.defs. Two output files are created:
#   /tmp/gdk-2.10-types.defs and /tmp/gdk-2.10.defs.
#
# python h2def.py -n WebKit /usr/incude/webkit-1.0/webkit/*.h \
#            >/tmp/webkit.defs
#
# - Outputs all the defs for webkit module, setting the module name to WebKit
#   and the gtype-id prefix to WEBKIT_ which can't be detected automatically.
#

import getopt
import os
import re
import string
import sys

import defsparser

# ------------------ Create typecodes from typenames ---------

_upperstr_pat1 = re.compile(r'([^A-Z])([A-Z])')
_upperstr_pat2 = re.compile(r'([A-Z][A-Z])([A-Z][0-9a-z])')
_upperstr_pat3 = re.compile(r'^([A-Z])([A-Z])')

def to_upper_str(name):
    """Converts a typename to the equivalent upercase and underscores
    name.  This is used to form the type conversion macros and enum/flag
    name variables"""
    name = _upperstr_pat1.sub(r'\1_\2', name)
    name = _upperstr_pat2.sub(r'\1_\2', name)
    name = _upperstr_pat3.sub(r'\1_\2', name, count=1)
    return string.upper(name)

def typecode(typename, namespace=None):
    """create a typecode (eg. GTK_TYPE_WIDGET) from a typename"""
    if namespace:
      return string.replace(string.upper(namespace) + "_" + to_upper_str(typename[len(namespace):]), '_', '_TYPE_', 1)

    return string.replace(to_upper_str(typename), '_', '_TYPE_', 1)


# ------------------ Find object definitions -----------------
# Strips the comments from buffer
def strip_comments(buf):
    parts = []
    lastpos = 0
    while 1:
        pos = string.find(buf, '/*', lastpos)
        if pos >= 0:
            parts.append(buf[lastpos:pos])
            pos = string.find(buf, '*/', pos)
            if pos >= 0:
                lastpos = pos + 2
            else:
                break
        else:
            parts.append(buf[lastpos:])
            break
    return string.join(parts, '')

# Strips the dll API from buffer, for example WEBKIT_API
def strip_dll_api(buf):
    pat = re.compile("[A-Z]*_API ")
    buf = pat.sub("", buf)
    return buf

obj_name_pat = "[A-Z][a-z]*[A-Z][A-Za-z0-9]*"

split_prefix_pat = re.compile('([A-Z]+[a-z]*)([A-Za-z0-9]+)')

def find_obj_defs(buf, objdefs=[]):
    """
    Try to find object definitions in header files.
    """

    # filter out comments from buffer.
    buf = strip_comments(buf)

    # filter out dll api
    buf = strip_dll_api(buf)

    maybeobjdefs = []  # contains all possible objects from file

    # first find all structures that look like they may represent a GtkObject
    pat = re.compile("struct\s+_(" + obj_name_pat + ")\s*{\s*" +
                     "(" + obj_name_pat + ")\s+", re.MULTILINE)
    pos = 0
    while pos < len(buf):
        m = pat.search(buf, pos)
        if not m: break
        maybeobjdefs.append((m.group(1), m.group(2)))
        pos = m.end()

    # handle typedef struct { ... } style struct defs.
    pat = re.compile("typedef struct\s+[_\w]*\s*{\s*" +
                     "(" + obj_name_pat + ")\s+[^}]*}\s*" +
                     "(" + obj_name_pat + ")\s*;", re.MULTILINE)
    pos = 0
    while pos < len(buf):
        m = pat.search(buf, pos)
        if not m: break
        maybeobjdefs.append((m.group(2), m.group(1)))
        pos = m.end()

    # now find all structures that look like they might represent a class:
    pat = re.compile("struct\s+_(" + obj_name_pat + ")Class\s*{\s*" +
                     "(" + obj_name_pat + ")Class\s+", re.MULTILINE)
    pos = 0
    while pos < len(buf):
        m = pat.search(buf, pos)
        if not m: break
        t = (m.group(1), m.group(2))
        # if we find an object structure together with a corresponding
        # class structure, then we have probably found a GtkObject subclass.
        if t in maybeobjdefs:
            objdefs.append(t)
        pos = m.end()

    pat = re.compile("typedef struct\s+[_\w]*\s*{\s*" +
                     "(" + obj_name_pat + ")Class\s+[^}]*}\s*" +
                     "(" + obj_name_pat + ")Class\s*;", re.MULTILINE)
    pos = 0
    while pos < len(buf):
        m = pat.search(buf, pos)
        if not m: break
        t = (m.group(2), m.group(1))
        # if we find an object structure together with a corresponding
        # class structure, then we have probably found a GtkObject subclass.
        if t in maybeobjdefs:
            objdefs.append(t)
        pos = m.end()

    # now find all structures that look like they might represent
    # a class inherited from GTypeInterface:
    pat = re.compile("struct\s+_(" + obj_name_pat + ")Class\s*{\s*" +
                     "GTypeInterface\s+", re.MULTILINE)
    pos = 0
    while pos < len(buf):
        m = pat.search(buf, pos)
        if not m: break
        t = (m.group(1), '')
        t2 = (m.group(1)+'Class', 'GTypeInterface')
        # if we find an object structure together with a corresponding
        # class structure, then we have probably found a GtkObject subclass.
        if t2 in maybeobjdefs:
            objdefs.append(t)
        pos = m.end()

    # now find all structures that look like they might represent
    # an Iface inherited from GTypeInterface:
    pat = re.compile("struct\s+_(" + obj_name_pat + ")Iface\s*{\s*" +
                     "GTypeInterface\s+", re.MULTILINE)
    pos = 0
    while pos < len(buf):
        m = pat.search(buf, pos)
        if not m: break
        t = (m.group(1), '')
        t2 = (m.group(1)+'Iface', 'GTypeInterface')
        # if we find an object structure together with a corresponding
        # class structure, then we have probably found a GtkObject subclass.
        if t2 in maybeobjdefs:
            objdefs.append(t)
        pos = m.end()

def sort_obj_defs(objdefs):
    objdefs.sort()  # not strictly needed, but looks nice
    pos = 0
    while pos < len(objdefs):
        klass,parent = objdefs[pos]
        for i in range(pos+1, len(objdefs)):
            # parent below subclass ... reorder
            if objdefs[i][0] == parent:
                objdefs.insert(i+1, objdefs[pos])
                del objdefs[pos]
                break
        else:
            pos = pos + 1
    return objdefs

# ------------------ Find enum definitions -----------------

def find_enum_defs(buf, enums=[]):
    # strip comments
    # bulk comments
    buf = strip_comments(buf)

    # strip dll api macros
    buf = strip_dll_api(buf)

    # strip # directives
    pat = re.compile(r"""^[#].*?$""", re.MULTILINE)
    buf = pat.sub('', buf)

    buf = re.sub('\n', ' ', buf)

    enum_pat = re.compile(r'enum\s*{([^}]*)}\s*([A-Z][A-Za-z]*)(\s|;)')
    splitter = re.compile(r'\s*,\s', re.MULTILINE)
    pos = 0
    while pos < len(buf):
        m = enum_pat.search(buf, pos)
        if not m: break

        name = m.group(2)
        vals = m.group(1)
        isflags = string.find(vals, '<<') >= 0
        entries = []
        for val in splitter.split(vals):
            if not string.strip(val): continue
            entries.append(string.split(val)[0])
        if name != 'GdkCursorType':
            enums.append((name, isflags, entries))

        pos = m.end()

# ------------------ Find function definitions -----------------

def clean_func(buf):
    """
    Ideally would make buf have a single prototype on each line.
    Actually just cuts out a good deal of junk, but leaves lines
    where a regex can figure prototypes out.
    """
    # bulk comments
    buf = strip_comments(buf)

    # dll api
    buf = strip_dll_api(buf)

    # compact continued lines
    pat = re.compile(r"""\\\n""", re.MULTILINE)
    buf = pat.sub('', buf)

    # Preprocess directives
    pat = re.compile(r"""^[#].*?$""", re.MULTILINE)
    buf = pat.sub('', buf)

    #typedefs, stucts, and enums
    pat = re.compile(r"""^(typedef|struct|enum)(\s|.|\n)*?;\s*""",
                     re.MULTILINE)
    buf = pat.sub('', buf)

    #strip DECLS macros
    pat = re.compile(r"""G_BEGIN_DECLS|BEGIN_LIBGTOP_DECLS""", re.MULTILINE)
    buf = pat.sub('', buf)

    #extern "C"
    pat = re.compile(r"""^\s*(extern)\s+\"C\"\s+{""", re.MULTILINE)
    buf = pat.sub('', buf)

    #multiple whitespace
    pat = re.compile(r"""\s+""", re.MULTILINE)
    buf = pat.sub(' ', buf)

    #clean up line ends
    pat = re.compile(r""";\s*""", re.MULTILINE)
    buf = pat.sub('\n', buf)
    buf = buf.lstrip()

    #associate *, &, and [] with type instead of variable
    #pat = re.compile(r'\s+([*|&]+)\s*(\w+)')
    pat = re.compile(r' \s* ([*|&]+) \s* (\w+)', re.VERBOSE)
    buf = pat.sub(r'\1 \2', buf)
    pat = re.compile(r'\s+ (\w+) \[ \s* \]', re.VERBOSE)
    buf = pat.sub(r'[] \1', buf)

    # make return types that are const work.
    buf = re.sub(r'\s*\*\s*G_CONST_RETURN\s*\*\s*', '** ', buf)
    buf = string.replace(buf, 'G_CONST_RETURN ', 'const-')
    buf = string.replace(buf, 'const ', 'const-')

    #strip GSEAL macros from the middle of function declarations:
    pat = re.compile(r"""GSEAL""", re.VERBOSE)
    buf = pat.sub('', buf)

    return buf

proto_pat=re.compile(r"""
(?P<ret>(-|\w|\&|\*)+\s*)  # return type
\s+                   # skip whitespace
(?P<func>\w+)\s*[(]   # match the function name until the opening (
\s*(?P<args>.*?)\s*[)]     # group the function arguments
""", re.IGNORECASE|re.VERBOSE)
#"""
arg_split_pat = re.compile("\s*,\s*")

get_type_pat = re.compile(r'(const-)?([A-Za-z0-9]+)\*?\s+')
pointer_pat = re.compile('.*\*$')
func_new_pat = re.compile('(\w+)_new$')

class DefsWriter:
    def __init__(self, fp=None, prefix=None, ns=None, verbose=False,
                 defsfilter=None):
        if not fp:
            fp = sys.stdout

        self.fp = fp
        self.prefix = prefix
        self.namespace = ns
        self.verbose = verbose

        self._enums = {}
        self._objects = {}
        self._functions = {}
        if defsfilter:
            filter = defsparser.DefsParser(defsfilter)
            filter.startParsing()
            for func in filter.functions + filter.methods.values():
                self._functions[func.c_name] = func
            for obj in filter.objects + filter.boxes + filter.interfaces:
                self._objects[obj.c_name] = obj
            for obj in filter.enums:
                self._enums[obj.c_name] = obj

    def write_def(self, deffile):
        buf = open(deffile).read()

        self.fp.write('\n;; From %s\n\n' % os.path.basename(deffile))
        self._define_func(buf)
        self.fp.write('\n')

    def write_enum_defs(self, enums, fp=None):
        if not fp:
            fp = self.fp

        fp.write(';; Enumerations and flags ...\n\n')
        trans = string.maketrans(string.uppercase + '_',
                                 string.lowercase + '-')
        filter = self._enums
        for cname, isflags, entries in enums:
            if filter:
                if cname in filter:
                    continue
            name = cname
            module = None
            if self.namespace:
                module = self.namespace
                name = cname[len(self.namespace):]
            else:
                m = split_prefix_pat.match(cname)
                if m:
                    module = m.group(1)
                    name = m.group(2)
            if isflags:
                fp.write('(define-flags ' + name + '\n')
            else:
                fp.write('(define-enum ' + name + '\n')
            if module:
                fp.write('  (in-module "' + module + '")\n')
            fp.write('  (c-name "' + cname + '")\n')
            fp.write('  (gtype-id "' + typecode(cname, self.namespace) + '")\n')
            prefix = entries[0]
            for ent in entries:
                # shorten prefix til we get a match ...
                # and handle GDK_FONT_FONT, GDK_FONT_FONTSET case
                while ((len(prefix) and prefix[-1] != '_') or ent[:len(prefix)] != prefix
                       or len(prefix) >= len(ent)):
                    prefix = prefix[:-1]
            prefix_len = len(prefix)
            fp.write('  (values\n')
            for ent in entries:
                fp.write('    \'("%s" "%s")\n' %
                         (string.translate(ent[prefix_len:], trans), ent))
            fp.write('  )\n')
            fp.write(')\n\n')

    def write_obj_defs(self, objdefs, fp=None):
        if not fp:
            fp = self.fp

        fp.write(';; -*- scheme -*-\n')
        fp.write('; object definitions ...\n')

        filter = self._objects
        for klass, parent in objdefs:
            if filter:
                if klass in filter:
                    continue
            if self.namespace:
                cname = klass[len(self.namespace):]
                cmodule = self.namespace
            else:
                m = split_prefix_pat.match(klass)
                cname = klass
                cmodule = None
                if m:
                    cmodule = m.group(1)
                    cname = m.group(2)
            fp.write('(define-object ' + cname + '\n')
            if cmodule:
                fp.write('  (in-module "' + cmodule + '")\n')
            if parent:
                fp.write('  (parent "' + parent + '")\n')
            fp.write('  (c-name "' + klass + '")\n')
            fp.write('  (gtype-id "' + typecode(klass, self.namespace) + '")\n')
            # should do something about accessible fields
            fp.write(')\n\n')

    def _define_func(self, buf):
        buf = clean_func(buf)
        buf = string.split(buf,'\n')
        filter = self._functions
        for p in buf:
            if not p:
                continue
            m = proto_pat.match(p)
            if m == None:
                if self.verbose:
                    sys.stderr.write('No match:|%s|\n' % p)
                continue
            func = m.group('func')
            if func[0] == '_':
                continue
            if filter:
                if func in filter:
                    continue
            ret = m.group('ret')
            args = m.group('args')
            args = arg_split_pat.split(args)
            for i in range(len(args)):
                spaces = string.count(args[i], ' ')
                if spaces > 1:
                    args[i] = string.replace(args[i], ' ', '-', spaces - 1)

            self._write_func(func, ret, args)

    def _write_func(self, name, ret, args):
        if len(args) >= 1:
            # methods must have at least one argument
            munged_name = name.replace('_', '')
            m = get_type_pat.match(args[0])
            if m:
                obj = m.group(2)
                if munged_name[:len(obj)] == obj.lower():
                    self._write_method(obj, name, ret, args)
                    return

        if self.prefix:
            l = len(self.prefix)
            if name[:l] == self.prefix and name[l] == '_':
                fname = name[l+1:]
            else:
                fname = name
        else:
            fname = name

        # it is either a constructor or normal function
        self.fp.write('(define-function ' + fname + '\n')
        self.fp.write('  (c-name "' + name + '")\n')

        # Hmmm... Let's asume that a constructor function name
        # ends with '_new' and it returns a pointer.
        m = func_new_pat.match(name)
        if pointer_pat.match(ret) and m:
            cname = ''
            for s in m.group(1).split ('_'):
                cname += s.title()
            if cname != '':
                self.fp.write('  (is-constructor-of "' + cname + '")\n')

        self._write_return(ret)
        self._write_arguments(args)

    def _write_method(self, obj, name, ret, args):
        regex = string.join(map(lambda x: x+'_?', string.lower(obj)),'')
        mname = re.sub(regex, '', name, 1)
        if self.prefix:
            l = len(self.prefix) + 1
            if mname[:l] == self.prefix and mname[l+1] == '_':
                mname = mname[l+1:]
        self.fp.write('(define-method ' + mname + '\n')
        self.fp.write('  (of-object "' + obj + '")\n')
        self.fp.write('  (c-name "' + name + '")\n')
        self._write_return(ret)
        self._write_arguments(args[1:])

    def _write_return(self, ret):
        if ret != 'void':
            self.fp.write('  (return-type "' + ret + '")\n')
        else:
            self.fp.write('  (return-type "none")\n')

    def _write_arguments(self, args):
        is_varargs = 0
        has_args = len(args) > 0
        for arg in args:
            if arg == '...':
                is_varargs = 1
            elif arg in ('void', 'void '):
                has_args = 0
        if has_args:
            self.fp.write('  (parameters\n')
            for arg in args:
                if arg != '...':
                    tupleArg = tuple(string.split(arg))
                    if len(tupleArg) == 2:
                        self.fp.write('    \'("%s" "%s")\n' % tupleArg)
            self.fp.write('  )\n')
        if is_varargs:
            self.fp.write('  (varargs #t)\n')
        self.fp.write(')\n\n')

# ------------------ Main function -----------------

def main(args):
    verbose = False
    onlyenums = False
    onlyobjdefs = False
    separate = False
    modulename = None
    namespace = None
    defsfilter = None
    opts, args = getopt.getopt(args[1:], 'vs:m:n:f:',
                               ['onlyenums', 'onlyobjdefs',
                                'modulename=', 'namespace=',
                                'separate=', 'defsfilter='])
    for o, v in opts:
        if o == '-v':
            verbose = True
        if o == '--onlyenums':
            onlyenums = True
        if o == '--onlyobjdefs':
            onlyobjdefs = True
        if o in ('-s', '--separate'):
            separate = v
        if o in ('-m', '--modulename'):
            modulename = v
        if o in ('-n', '--namespace'):
            namespace = v
        if o in ('-f', '--defsfilter'):
            defsfilter = v

    if not args[0:1]:
        print 'Must specify at least one input file name'
        return -1

    # read all the object definitions in
    objdefs = []
    enums = []
    for filename in args:
        buf = open(filename).read()
        find_obj_defs(buf, objdefs)
        find_enum_defs(buf, enums)
    objdefs = sort_obj_defs(objdefs)

    if separate:
        methods = file(separate + '.defs', 'w')
        types = file(separate + '-types.defs', 'w')

        dw = DefsWriter(methods, prefix=modulename, ns=namespace,
                        verbose=verbose, defsfilter=defsfilter)
        dw.write_obj_defs(objdefs, types)
        dw.write_enum_defs(enums, types)
        print "Wrote %s-types.defs" % separate

        for filename in args:
            dw.write_def(filename)
        print "Wrote %s.defs" % separate
    else:
        dw = DefsWriter(prefix=modulename, ns=namespace,
                        verbose=verbose, defsfilter=defsfilter)

        if onlyenums:
            dw.write_enum_defs(enums)
        elif onlyobjdefs:
            dw.write_obj_defs(objdefs)
        else:
            dw.write_obj_defs(objdefs)
            dw.write_enum_defs(enums)

            for filename in args:
                dw.write_def(filename)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
