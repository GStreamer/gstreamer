#!/usr/bin/env python
# -*- Mode: Python; py-indent-offset: 4 -*-
# Search through a header file looking for function prototypes.
# For each prototype, generate a scheme style definition.
# GPL'ed
# Toby D. Reeves <toby@max.rl.plh.af.mil>

# Modified by James Henstridge <james@daa.com.au> to output stuff in
# Havoc's new defs format.  Info on this format can be seen at:
#   http://www.gnome.org/mailing-lists/archives/gtk-devel-list/2000-January/0085.shtml


import string, sys, re, types

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

def typecode(typename):
    """create a typecode (eg. GTK_TYPE_WIDGET) from a typename"""
    return string.replace(to_upper_str(typename), '_', '_TYPE_', 1)


# ------------------ Find object definitions -----------------

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

obj_name_pat = "[A-Z][a-z]*[A-Z][A-Za-z0-9]*"

split_prefix_pat = re.compile('([A-Z][a-z]*)([A-Za-z0-9]+)')

def find_obj_defs(buf, objdefs=[]):
    """
    Try to find object definitions in header files.
    """

    # filter out comments from buffer.
    buf = strip_comments(buf)

    maybeobjdefs = []  # contains all possible objects from file

    # first find all structures that look like they may represent a GtkObject
    pat = re.compile("struct _(" + obj_name_pat + ")\s*{\s*" +
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
        maybeobjdefs.append((m.group(2), m.group(2)))
        pos = m.end()

    # now find all structures that look like they might represent a class:
    pat = re.compile("struct _(" + obj_name_pat + ")Class\s*{\s*" +
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

    # now find all structures that look like they might represent a class inherited from GTypeInterface:
    pat = re.compile("struct _(" + obj_name_pat + ")Class\s*{\s*" +
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

    # now find all structures that look like they might represent an Iface inherited from GTypeInterface:
    pat = re.compile("struct _(" + obj_name_pat + ")Iface\s*{\s*" +
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

def write_obj_defs(objdefs, output):
    if type(output)==types.StringType:
        fp=open(output,'w')
    elif type(output)==types.FileType:
        fp=output
    else:
        fp=sys.stdout

    fp.write(';; -*- scheme -*-\n')
    fp.write('; object definitions ...\n')

    for klass, parent in objdefs:
        m = split_prefix_pat.match(klass)
        cmodule = None
        cname = klass
        if m:
            cmodule = m.group(1)
            cname = m.group(2)

        fp.write('(define-object ' + cname + '\n')
        if cmodule:
            fp.write('  (in-module "' + cmodule + '")\n')
        if parent:
            fp.write('  (parent "' + parent + '")\n')
        fp.write('  (c-name "' + klass + '")\n')
        fp.write('  (gtype-id "' + typecode(klass) + '")\n')
        # should do something about accessible fields
        fp.write(')\n\n')

# ------------------ Find enum definitions -----------------

def find_enum_defs(buf, enums=[]):
    # strip comments
    # bulk comments
    buf = strip_comments(buf)

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

def write_enum_defs(enums, output=None):
    if type(output)==types.StringType:
        fp=open(output,'w')
    elif type(output)==types.FileType:
        fp=output
    else:
        fp=sys.stdout

    fp.write(';; Enumerations and flags ...\n\n')
    trans = string.maketrans(string.uppercase + '_', string.lowercase + '-')
    for cname, isflags, entries in enums:
        name = cname
        module = None
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
        fp.write('  (gtype-id "' + typecode(cname) + '")\n')
        prefix = entries[0]
        for ent in entries:
            # shorten prefix til we get a match ...
            # and handle GDK_FONT_FONT, GDK_FONT_FONTSET case
            while ent[:len(prefix)] != prefix or len(prefix) >= len(ent):
                prefix = prefix[:-1]
        prefix_len = len(prefix)
        fp.write('  (values\n')
        for ent in entries:
            fp.write('    \'("%s" "%s")\n' %
                     (string.translate(ent[prefix_len:], trans), ent))
        fp.write('  )\n')
        fp.write(')\n\n')

# ------------------ Find function definitions -----------------

def clean_func(buf):
    """
    Ideally would make buf have a single prototype on each line.
    Actually just cuts out a good deal of junk, but leaves lines
    where a regex can figure prototypes out.
    """
    # bulk comments
    buf = strip_comments(buf)

    # compact continued lines
    pat = re.compile(r"""\\\n""", re.MULTILINE) 
    buf=pat.sub('',buf)

    # Preprocess directives
    pat = re.compile(r"""^[#].*?$""", re.MULTILINE) 
    buf=pat.sub('',buf)

    #typedefs, stucts, and enums
    pat = re.compile(r"""^(typedef|struct|enum)(\s|.|\n)*?;\s*""", re.MULTILINE) 
    buf=pat.sub('',buf)

    #strip DECLS macros
    pat = re.compile(r"""G_BEGIN_DECLS|BEGIN_LIBGTOP_DECLS""", re.MULTILINE) 
    buf=pat.sub('',buf)

    #extern "C"
    pat = re.compile(r"""^\s*(extern)\s+\"C\"\s+{""", re.MULTILINE) 
    buf=pat.sub('',buf)

    #multiple whitespace
    pat = re.compile(r"""\s+""", re.MULTILINE) 
    buf=pat.sub(' ',buf)

    #clean up line ends
    pat = re.compile(r""";\s*""", re.MULTILINE) 
    buf=pat.sub('\n',buf)
    buf = buf.lstrip()

    #associate *, &, and [] with type instead of variable
    #pat=re.compile(r'\s+([*|&]+)\s*(\w+)')
    pat=re.compile(r' \s* ([*|&]+) \s* (\w+)',re.VERBOSE)
    buf=pat.sub(r'\1 \2', buf)
    pat=re.compile(r'\s+ (\w+) \[ \s* \]',re.VERBOSE)
    buf=pat.sub(r'[] \1', buf)

    # make return types that are const work.
    buf = string.replace(buf, 'G_CONST_RETURN ', 'const-')
    buf = string.replace(buf, 'const ', 'const-')

    return buf

proto_pat=re.compile(r"""
(?P<ret>(-|\w|\&|\*)+\s*)  # return type
\s+                   # skip whitespace
(?P<func>\w+)\s*[(]   # match the function name until the opening (
(?P<args>.*?)[)]     # group the function arguments
""", re.IGNORECASE|re.VERBOSE)
#"""
arg_split_pat = re.compile("\s*,\s*")

def define_func(buf,fp, prefix):
    buf=clean_func(buf)
    buf=string.split(buf,'\n')
    for p in buf:
        if len(p)==0: continue
        m=proto_pat.match(p)
        if m==None:
            if verbose:
                sys.stderr.write('No match:|%s|\n'%p)
            continue
        func = m.group('func')
        if func[0] == '_':
            continue
        ret = m.group('ret')
        args=m.group('args')
        args=arg_split_pat.split(args)
        for i in range(len(args)):
            spaces = string.count(args[i], ' ')
            if spaces > 1:
                args[i] = string.replace(args[i], ' ', '-', spaces - 1)
                
        write_func(fp, func, ret, args, prefix)

get_type_pat = re.compile(r'(const-)?([A-Za-z0-9]+)\*?\s+')
pointer_pat = re.compile('.*\*$')
func_new_pat = re.compile('(\w+)_new$')

def write_func(fp, name, ret, args, prefix):
    if len(args) >= 1:
        # methods must have at least one argument
        munged_name = string.replace(name, '_', '')
        m = get_type_pat.match(args[0])
        if m:
            obj = m.group(2)
            if munged_name[:len(obj)] == string.lower(obj):
                regex = string.join(map(lambda x: x+'_?',string.lower(obj)),'')
                mname = re.sub(regex, '', name)
                if prefix:
                    l = len(prefix) + 1
                    if mname[:l] == prefix and mname[l+1] == '_':
                        mname = mname[l+1:]
                fp.write('(define-method ' + mname + '\n')
                fp.write('  (of-object "' + obj + '")\n')
                fp.write('  (c-name "' + name + '")\n')
                if ret != 'void':
                    fp.write('  (return-type "' + ret + '")\n')
                else:
                    fp.write('  (return-type "none")\n')
                is_varargs = 0
                has_args = len(args) > 1
                for arg in args[1:]:
                    if arg == '...':
                        is_varargs = 1
                    elif arg in ('void', 'void '):
                        has_args = 0
                if has_args:
                    fp.write('  (parameters\n')
                    for arg in args[1:]:
                        if arg != '...':
                            tupleArg = tuple(string.split(arg))
                            if len(tupleArg) == 2:
                                fp.write('    \'("%s" "%s")\n' % tupleArg)
                    fp.write('  )\n')
                if is_varargs:
                    fp.write('  (varargs #t)\n')
                fp.write(')\n\n')
                return
    if prefix:
        l = len(prefix)
        if name[:l] == prefix and name[l] == '_':
            fname = name[l+1:]
        else:
            fname = name
    else:
        fname = name
    # it is either a constructor or normal function
    fp.write('(define-function ' + fname + '\n')
    fp.write('  (c-name "' + name + '")\n')

    # Hmmm... Let's asume that a constructor function name
    # ends with '_new' and it returns a pointer.
    m = func_new_pat.match(name)
    if pointer_pat.match(ret) and m:
        cname = ''
	for s in m.group(1).split ('_'):
	    cname += s.title()
	if cname != '':
	    fp.write('  (is-constructor-of "' + cname + '")\n')

    if ret != 'void':
        fp.write('  (return-type "' + ret + '")\n')
    else:
        fp.write('  (return-type "none")\n')
    is_varargs = 0
    has_args = len(args) > 0
    for arg in args:
        if arg == '...':
            is_varargs = 1
        elif arg in ('void', 'void '):
            has_args = 0
    if has_args:
        fp.write('  (parameters\n')
        for arg in args:
            if arg != '...':
                tupleArg = tuple(string.split(arg))
                if len(tupleArg) == 2:
                    fp.write('    \'("%s" "%s")\n' % tupleArg)
        fp.write('  )\n')
    if is_varargs:
        fp.write('  (varargs #t)\n')
    fp.write(')\n\n')

def write_def(input,output=None, prefix=None):
    fp = open(input)
    buf = fp.read()
    fp.close()

    if type(output) == types.StringType:
        fp = open(output,'w')
    elif type(output) == types.FileType:
        fp = output
    else:
        fp = sys.stdout

    fp.write('\n;; From %s\n\n' % input)
    buf = define_func(buf, fp, prefix)
    fp.write('\n')

# ------------------ Main function -----------------

verbose=0
def main(args):
    import getopt
    global verbose

    onlyenums = 0
    onlyobjdefs = 0
    separate = 0
    modulename = None
    opts, args = getopt.getopt(args[1:], 'vs:m:',
                               ['onlyenums', 'onlyobjdefs',
                                'modulename=', 'separate='])
    for o, v in opts:
        if o == '-v':
            verbose = 1
        if o == '--onlyenums':
            onlyenums = 1
        if o == '--onlyobjdefs':
            onlyobjdefs = 1
        if o in ('-s', '--separate'):
            separate = v
        if o in ('-m', '--modulename'):
            modulename = v
            
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
        types = file(separate + '-types.defs', 'w')
        methods = file(separate + '.defs', 'w')
        
        write_obj_defs(objdefs,types)
        write_enum_defs(enums,types)
        types.close()
        print "Wrote %s-types.defs" % separate
        
        for filename in args:
            write_def(filename,methods,prefix=modulename)
        methods.close()
        print "Wrote %s.defs" % separate
    else:
        if onlyenums:
            write_enum_defs(enums,None)
        elif onlyobjdefs:
            write_obj_defs(objdefs,None)
        else:
            write_obj_defs(objdefs,None)
            write_enum_defs(enums,None)

            for filename in args:
                write_def(filename,None,prefix=modulename)
            
if __name__ == '__main__':
    sys.exit(main(sys.argv))
