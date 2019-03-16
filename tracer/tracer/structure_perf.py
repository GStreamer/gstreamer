import timeit

from structure import Structure
from gi.repository import Gst
Gst.init(None)

PLAIN_STRUCTURE = r'thread-rusage, thread-id=(guint64)37268592, ts=(guint64)79416000, average-cpuload=(uint)1000, current-cpuload=(uint)1000, time=(guint64)79418045;'
NESTED_STRUCTURE = r'latency.class, src=(structure)"scope\,\ type\=\(type\)gchararray\,\ related-to\=\(GstTracerValueScope\)GST_TRACER_VALUE_SCOPE_PAD\;", sink=(structure)"scope\,\ type\=\(type\)gchararray\,\ related-to\=\(GstTracerValueScope\)GST_TRACER_VALUE_SCOPE_PAD\;", time=(structure)"value\,\ type\=\(type\)guint64\,\ description\=\(string\)\"time\\\ it\\\ took\\\ for\\\ the\\\ buffer\\\ to\\\ go\\\ from\\\ src\\\ to\\\ sink\\\ ns\"\,\ flags\=\(GstTracerValueFlags\)GST_TRACER_VALUE_FLAGS_AGGREGATED\,\ min\=\(guint64\)0\,\ max\=\(guint64\)18446744073709551615\;";'

NAT_STRUCTURE = Structure(PLAIN_STRUCTURE)
GI_STRUCTURE = Gst.Structure.from_string(PLAIN_STRUCTURE)[0]


# native python impl

def nat_parse_plain():
    s = Structure(PLAIN_STRUCTURE)


def nat_parse_nested():
    s = Structure(NESTED_STRUCTURE)


def nat_get_name():
    return NAT_STRUCTURE.name


def nat_get_value():
    return NAT_STRUCTURE.values['thread-id']


# gstreamer impl via gi

def gi_parse_plain():
    s = Gst.Structure.from_string(PLAIN_STRUCTURE)[0]


def gi_parse_nested():
    s = Gst.Structure.from_string(NESTED_STRUCTURE)[0]


def gi_get_name():
    return GI_STRUCTURE.get_name()


def gi_get_value():
    return GI_STRUCTURE.get_value('thread-id')


# perf test

def perf(method, n, flavor):
    t = timeit.timeit(method + '()', 'from __main__ import ' + method, number=n)
    print("%6s: %lf s, (%lf calls/s)" % (flavor, t, (n / t)))


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('-i', '--iterations', default=10000, type=int,
                        help='number of iterations (default: 10000)')
    args = parser.parse_args()
    n = args.iterations

    print("parse_plain:")
    t = perf('nat_parse_plain', n, 'native')
    t = perf('gi_parse_plain', n, 'gi')

    print("parse_nested:")
    t = perf('nat_parse_nested', n, 'native')
    t = perf('gi_parse_nested', n, 'gi')

    print("get_name:")
    t = perf('nat_get_name', n, 'native')
    t = perf('gi_get_name', n, 'gi')

    print("get_value:")
    t = perf('nat_get_value', n, 'native')
    t = perf('gi_get_value', n, 'gi')
