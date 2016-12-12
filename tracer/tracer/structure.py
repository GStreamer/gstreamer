import re

class Structure(object):
    '''Gst Structure parser.'''

    def __init__(self, text):
        self.text = text
        self.name = None
        self.types = {}
        self.values = {}
        self.pos = 0
        self.valid = False
        try:
            self._parse(self.text)
            self.valid = True
        except ValueError:
            pass

    def __repr__(self):
        return self.text

    def _parse(self, s):
        scan = True
        # parse id
        p = s.find(',')
        if p == -1:
            p = s.index(';')
            scan = False
        self.name = s[:p]
        s = s[(p + 2):]  # skip 'name, '
        self.pos += p + 2
        # parse fields
        while scan:
            p = s.index('=')
            k = s[:p]
            s = s[(p + 1):]  # skip 'key='
            self.pos += p + 1
            p = s.index('(')
            s = s[(p + 1):]  # skip '('
            self.pos += p + 1
            p = s.index(')')
            t = s[:p]
            s = s[(p + 1):]  # skip 'type)'
            self.pos += p + 1
            if t == 'structure':
                p = s.index('"')
                s = s[(p + 1):]  # skip '"'
                self.pos += p + 1
                # find next '"' without preceeding '\'
                sub = s
                sublen = 0
                while True:
                    p = sub.index('"')
                    sublen += p + 1
                    if sub[p - 1] != '\\':
                        sub = None
                        break;
                    sub = sub[(p + 1):]
                if not sub:
                    sub = s[:(sublen - 1)]
                    # unescape \., but not \\. (using a backref)
                    # FIXME: try to combine
                    # also:
                    # unescape = re.compile('search')
                    # unescape.sub('replacement', sub)
                    sub = re.sub(r'\\\\', r'\\', sub)
                    sub = re.sub(r'(?<!\\)\\(.)', r'\1', sub)
                    sub = re.sub(r'(?<!\\)\\(.)', r'\1', sub)
                    # recurse
                    v = Structure(sub)
                    if s[sublen] == ';':
                        scan = False
                    s = s[(sublen + 2):]
                    self.pos += sublen + 2
                else:
                    raise ValueError
            else:
                p = s.find(',')
                if p == -1:
                    p = s.index(';')
                    scan = False
                v= s[:p]
                s = s[(p + 2):]  # skip "value, "
                self.pos += p + 2
                if t == 'string' and v[0] == '"':
                    v = v[1:-1]
                elif t in ['int', 'uint', 'int8', 'uint8', 'int16', 'uint16', 'int32', 'uint32', 'int64', 'uint64' ]:
                    v = int(v)
            self.types[k] = t
            self.values[k] = v

        self.valid = True
