import logging
import re

logger = logging.getLogger('structure')

UNESCAPE = re.compile(r'(?<!\\)\\(.)')

INT_TYPES = "".join(
    ("int", "uint", "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64")
)


class Structure(object):
    """
    Gst Structure parser.

    Has publicly accesible members representing the structure data:
    name -- the structure name
    types -- a dictionary keyed by the field name
    values -- a dictionary keyed by the field name
    """

    def __init__(self, text):
        self.text = text
        self.name, self.types, self.values = Structure._parse(text)

    def __repr__(self):
        return self.text

    @staticmethod
    def _find_eos(s):
        # find next '"' without preceeding '\'
        i = 0
        # logger.debug("find_eos: '%s'", s)
        while True:  # faster than regexp for '[^\\]\"'
            p = s.index('"')
            i += p + 1
            if s[p - 1] != '\\':
                # logger.debug("... ok  : '%s'", s[p:])
                return i
            s = s[(p + 1):]
            # logger.debug("...     : '%s'", s)
        return -1

    @staticmethod
    def _parse(s):
        types = {}
        values = {}
        scan = True
        # logger.debug("===: '%s'", s)
        # parse id
        p = s.find(',')
        if p == -1:
            p = s.index(';')
            scan = False
        name = s[:p]
        # parse fields
        while scan:
            s = s[(p + 2):]  # skip 'name, ' / 'value, '
            # logger.debug("...: '%s'", s)
            p = s.index('=')
            k = s[:p]
            if not s[p + 1] == '(':
                raise ValueError
            s = s[(p + 2):]  # skip 'key=('
            p = s.index(')')
            t = s[:p]
            s = s[(p + 1):]  # skip 'type)'

            if s[0] == '"':
                s = s[1:]  # skip '"'
                p = Structure._find_eos(s)
                if p == -1:
                    raise ValueError
                v = s[:(p - 1)]
                if s[p] == ';':
                    scan = False
                # unescape \., but not \\. (using a backref)
                # need a reverse for re.escape()
                v = v.replace('\\\\', '\\')
                v = UNESCAPE.sub(r'\1', v)
            else:
                p = s.find(',')
                if p == -1:
                    p = s.index(';')
                    scan = False
                v = s[:p]

            if t == 'structure':
                v = Structure(v)
            elif t == 'string' and v[0] == '"':
                v = v[1:-1]
            elif t == 'boolean':
                v = (v == '1')
            elif t in INT_TYPES:
                v = int(v)
            types[k] = t
            values[k] = v
        return (name, types, values)
