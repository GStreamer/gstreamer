import sys
import string, re

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


STATE_START = 0
STATE_OBJECT = 1
STATE_INTERFACE = 2
STATE_BOXED = 3
STATE_ENUM = 4
STATE_FLAGS = 5
STATE_METHOD = 6
STATE_FUNCTION = 7
STATE_MINIOBJECT = 8

def convert(infp=sys.stdin, outfp=sys.stdout):
    state = STATE_START
    seen_params = 0

    line = infp.readline()
    while line:
	if line[:8] == '(object ':
	    state = STATE_OBJECT
	    seen_params = 0
	    outfp.write('(define-object ' + line[8:])
        elif line[:13] == '(mini-object ':
            state = STATE_MINI_OBJECT
            seen_params = 0
            outfp.write('(define mini-object ' + line[13:])
	elif line[:11] == '(interface ':
	    state = STATE_INTERFACE
	    seen_params = 0
	    outfp.write('(define-interface ' + line[11:])
	elif line[:7] == '(boxed ':
	    state = STATE_BOXED
	    seen_params = 0
	    outfp.write('(define-boxed ' + line[7:])
	elif line[:6] == '(enum ':
	    state = STATE_ENUM
	    seen_params = 0
	    outfp.write('(define-enum ' + line[6:])
	elif line[:7] == '(flags ':
	    state = STATE_FLAGS
	    seen_params = 0
	    outfp.write('(define-flags ' + line[7:])
	elif line[:8] == '(method ':
	    state = STATE_METHOD
	    seen_params = 0
	    outfp.write('(define-method ' + line[8:])
	elif line[:10] == '(function ':
	    state = STATE_FUNCTION
	    seen_params = 0
	    outfp.write('(define-function ' + line[10:])
	elif line[:13] == '  (in-module ':
	    outfp.write(re.sub(r'^(\s+\(in-module\s+)(\w+)(.*)$',
			       r'\1"\2"\3', line))
	elif line[:10] == '  (parent ':
	    outfp.write(re.sub(r'^(\s+\(parent\s+)(\w+)(\s+\((\w+)\))?(.*)$',
			       r'\1"\4\2"\5', line))
	elif line[:14] == '  (implements ':
	    outfp.write(re.sub(r'^(\s+\(implements\s+)([^\s]+)(\s*\))$',
			       r'\1"\2"\3', line))
	elif line[:13] == '  (of-object ':
	    outfp.write(re.sub(r'^(\s+\(of-object\s+)(\w+)(\s+\((\w+)\))?(.*)$',
			       r'\1"\4\2"\5', line))
	elif line[:10] == '  (c-name ':
	    outfp.write(re.sub(r'^(\s+\(c-name\s+)([^\s]+)(\s*\))$',
			       r'\1"\2"\3', line))
	    if state in (STATE_OBJECT, STATE_INTERFACE, STATE_BOXED,
			 STATE_ENUM, STATE_FLAGS):
		c_name = re.match(r'^\s+\(c-name\s+([^\s]+)\s*\)$',
				  line).group(1)
		outfp.write('  (gtype-id "%s")\n' % typecode(c_name))
	elif line[:15] == '  (return-type ':
	    outfp.write(re.sub(r'^(\s+\(return-type\s+)([^\s]+)(\s*\))$',
			       r'\1"\2"\3', line))
	elif line[:13] == '  (copy-func ':
	    outfp.write(re.sub(r'^(\s+\(copy-func\s+)(\w+)(.*)$',
			       r'\1"\2"\3', line))
	elif line[:16] == '  (release-func ':
	    outfp.write(re.sub(r'^(\s+\(release-func\s+)(\w+)(.*)$',
			       r'\1"\2"\3', line))
	elif line[:9] == '  (field ':
	    if not seen_params:
		outfp.write('  (fields\n')
	    seen_params = 1
	    outfp.write(re.sub(r'^\s+\(field\s+\(type-and-name\s+([^\s]+)\s+([^\s]+)\s*\)\s*\)$',
			       '    \'("\\1" "\\2")', line))
	elif line[:9] == '  (value ':
	    if not seen_params:
		outfp.write('  (values\n')
	    seen_params = 1
	    outfp.write(re.sub(r'^\s+\(value\s+\(name\s+([^\s]+)\)\s+\(c-name\s+([^\s]+)\s*\)\s*\)$',
			       '    \'("\\1" "\\2")', line))
	elif line[:13] == '  (parameter ':
	    if not seen_params:
		outfp.write('  (parameters\n')
	    seen_params = 1
	    outfp.write(re.sub(r'^\s+\(parameter\s+\(type-and-name\s+([^\s]+)\s+([^\s]+)\s*\)(\s*.*)\)$',
			       '    \'("\\1" "\\2"\\3)', line))
	elif line[:11] == '  (varargs ':
	    if seen_params:
		outfp.write('  )\n')
	    seen_params = 0
	    outfp.write('  (varargs #t)\n')
	elif line[0] == ')':
	    if seen_params:
		outfp.write('  )\n')
	    seen_params = 0
	    state = STATE_START
	    outfp.write(line)
	else:
	    outfp.write(line)
	line = infp.readline()

if __name__ == '__main__':
    convert()
