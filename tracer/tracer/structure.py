import logging
import re

logger = logging.getLogger('structure')

UNESCAPE = re.compile(r'(?<!\\)\\(.)')

INT_TYPES = "".join(
    ("int", "uint", "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64")
)

class Structure(object):
    '''Gst Structure parser.'''

    def __init__(self, text):
        self.text = text
        self.name = None
        self.types = {}
        self.values = {}
        self.pos = 0
        self._parse(self.text)

    def __repr__(self):
        return self.text

    def _find_eos(self, s):
        # find next '"' without preceeding '\'
        l = 0
        #logger.debug("find_eos: '%s'", s)
        while 1:  # faster than regexp for '[^\\]\"'
            p = s.index('"')
            l += p + 1
            if s[p - 1] != '\\':
                #logger.debug("... ok  : '%s'", s[p:])
                return l
            s = s[(p + 1):]
            #logger.debug("...     : '%s'", s)
        return -1

    def _parse(self, s):
        scan = True
        #logger.debug("===: '%s'", s)
        # parse id
        p = s.find(',')
        if p == -1:
            p = s.index(';')
            scan = False
        self.name = s[:p]
        # parse fields
        while scan:
            s = s[(p + 2):]  # skip 'name, ' / 'value, '
            self.pos += p + 2
            #logger.debug("...: '%s'", s)
            p = s.index('=')
            k = s[:p]
            if not s[p + 1] == '(':
                self.pos += p + 1
                raise ValueError
            s = s[(p + 2):]  # skip 'key=('
            self.pos += p + 2
            p = s.index(')')
            t = s[:p]
            s = s[(p + 1):]  # skip 'type)'
            self.pos += p + 1

            if s[0] == '"':
                s = s[1:]  # skip '"'
                self.pos += 1
                p = self._find_eos(s)
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
                v= s[:p]

            if t == 'structure':
                v = Structure(v)
            elif t == 'string' and v[0] == '"':
                v = v[1:-1]
            elif t in INT_TYPES:
                v = int(v)
            self.types[k] = t
            self.values[k] = v
        self.pos += p + 1
