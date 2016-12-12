#!/usr/bin/env python3
'''
How to run:
Offline:
GST_DEBUG="GST_TRACER:7" GST_TRACERS="stats;rusage;latency" GST_DEBUG_FILE=trace.log <application>
python3 gsttr-stats.py trace.log
'''

from tracer.analyzer import Analyzer
from tracer.parser import Parser

def format_ts(ts):
    sec = 1e9
    h = int(ts // (sec * 60 * 60))
    m = int((ts // (sec * 60)) % 60)
    s = (ts / sec)
    return '{:02d}.{:02d}.{:010.7f}'.format(h,m,s)

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('file', nargs='?', default='debug.log')
    args = parser.parse_args()

    with Parser(args.file) as log:
        stats = Analyzer(log)
        stats.run()

    # iterate scopes
    for sk,sv in stats.data.items():
        # iterate tracers
        for tk,tv in sv.items():
            mi = tv.get('min', '-')
            ma = tv.get('max', '-')
            avg = tv['sum']/tv['num']
            # TODO: need proper units
            if tk.endswith('/time') or tk.endswith('-dts') or tk.endswith('-pts'):
                if mi != '-':
                    mi = format_ts(mi)
                if ma != '-':
                    ma = format_ts(ma)
                avg = format_ts(avg)
            print("%-45s: Min/Avg,Max %30s: %s, %s, %s" % (sk, tk, mi, avg, ma))
