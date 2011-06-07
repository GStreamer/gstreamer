from __future__ import generators
import sys, os

def read_symbols(file, type=None, dynamic=0):
    if dynamic:
        cmd = 'nm -D %s' % file
    else:
        cmd = 'nm %s' % file
    for line in os.popen(cmd, 'r'):
        if line[0] != ' ':  # has an address as first bit of line
            while line[0] != ' ':
                line = line[1:]
        while line[0] == ' ':
            line = line[1:]
        # we should be up to "type symbolname" now
        sym_type = line[0]
        symbol = line[1:].strip()

        if not type or type == sym_type:
            yield symbol

def main():
    if len(sys.argv) != 3:
        sys.stderr.write('usage: coverage-check library.so wrapper.so\n')
        sys.exit(1)
    library = sys.argv[1]
    wrapper = sys.argv[2]

    # first create a dict with all referenced symbols in the wrapper
    # should really be a set, but a dict will do ...
    wrapper_symbols = {}
    for symbol in read_symbols(wrapper, type='U', dynamic=1):
        wrapper_symbols[symbol] = 1

    # now go through the library looking for matches on the defined symbols:
    for symbol in read_symbols(library, type='T', dynamic=1):
        if symbol[0] == '_': continue
        if symbol not in wrapper_symbols:
            print symbol

if __name__ == '__main__':
    main()
