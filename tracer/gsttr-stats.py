#!/usr/bin/env python3
'''
How to run:
1) generate some log
GST_DEBUG="GST_TRACER:7" GST_TRACERS="stats;rusage;latency" GST_DEBUG_FILE=trace.log <application>

2) print everything
python3 gsttr-stats.py trace.log

3) print selected entries only
python3 gsttr-stats.py -c latency trace.log
'''

import logging
from fnmatch import fnmatch

# TODO:
# more options
# - live-update interval (for file=='-')
#
# - for values like timestamps, we only want min/max but no average

logging.basicConfig(level=logging.WARNING)
logger = logging.getLogger('gsttr-stats')

from tracer.analysis_runner import AnalysisRunner
from tracer.analyzer import Analyzer
from tracer.parser import Parser
from tracer.structure import Structure

_SCOPE_RELATED_TO = {
    'GST_TRACER_VALUE_SCOPE_PAD': 'Pad',
    'GST_TRACER_VALUE_SCOPE_ELEMENT': 'Element',
    'GST_TRACER_VALUE_SCOPE_THREAD': 'Thread',
    'GST_TRACER_VALUE_SCOPE_PROCESS': 'Process',
}

_NUMERIC_TYPES = ('int', 'uint', 'gint', 'guint', 'gint64', 'guint64')

class Stats(Analyzer):

    def __init__(self, classes):
        super(Stats, self).__init__()
        self.classes = classes
        self.records = {}
        self.data = {}

    def handle_tracer_class(self, event):
        s = Structure(event[Parser.F_MESSAGE])
        # TODO only for debugging
        #print("tracer class:", repr(s))
        name = s.name[:-len('.class')]
        record = {
            'class': s,
            'scope' : {},
            'value' : {},
        }
        self.records[name] = record
        for k,v in s.values.items():
            if v.name == 'scope':
                # TODO only for debugging
                #print("scope: [%s]=%s" % (k, v))
                record['scope'][k] = v
            elif v.name == 'value':
                # skip non numeric and those without min/max
                # TODO: skip them only if flags != AGGREGATED
                # - if flag is AGGREGATED, don't calc average
                if (v.values['type'] in _NUMERIC_TYPES and
                      'min' in v.values and 'max' in v.values):
                    # TODO only for debugging
                    #print("value: [%s]=%s" % (k, v))
                    record['value'][k] = v
                #else:
                    # TODO only for debugging
                    #print("skipping value: [%s]=%s" % (k, v))

    def handle_tracer_entry(self, event):
        # use first field in message (structure-id) if none
        if event[Parser.F_FUNCTION]:
            return

        try:
            s = Structure(event[Parser.F_MESSAGE])
        except ValueError:
            logger.warning("failed to parse: '%s'", event[Parser.F_MESSAGE])
            return
        entry_name = s.name

        if self.classes:
            if not any([fnmatch(entry_name, c) for c in self.classes]):
                return

        record = self.records.get(entry_name)
        if not record:
            return

        # aggregate event based on class
        for sk,sv in record['scope'].items():
            # look up bin by scope (or create new)
            key = (_SCOPE_RELATED_TO[sv.values['related-to']] +
                ":" + str(s.values[sk]))
            scope = self.data.get(key)
            if not scope:
                scope = {}
                self.data[key] = scope
            for vk,vv in record['value'].items():
                # skip optional fields
                if not vk in s.values:
                    continue

                key = entry_name + "/" + vk
                data = scope.get(key)
                if not data:
                    data = {
                        'num': 0,
                        'sum': 0,
                    }
                    if 'max' in vv.values and 'min' in vv.values:
                        data['min'] = int(vv.values['max'])
                        data['max'] = int(vv.values['min'])
                    scope[key] = data
                # update min/max/sum and count via value
                dv = int(s.values[vk])
                data['num'] += 1
                data['sum'] += dv
                if 'min' in data:
                    data['min'] = min(dv, data['min'])
                if 'max' in data:
                    data['max'] = max(dv, data['max'])

    def report(self):
        # iterate scopes
        for sk,sv in self.data.items():
            # iterate tracers
            for tk,tv in sv.items():
                mi = tv.get('min', '-')
                ma = tv.get('max', '-')
                avg = tv['sum']/tv['num']
                if is_time_field(tk):
                    if mi != '-':
                        mi = format_ts(mi)
                    if ma != '-':
                        ma = format_ts(ma)
                    avg = format_ts(avg)
                if mi == ma:
                    print("%-45s: Avg %30s: %s" % (sk, tk, avg))
                else:
                    print("%-45s: Min/Avg/Max %30s: %s, %s, %s" %
                        (sk, tk, mi, avg, ma))

class ListClasses(Analyzer):

    def __init__(self):
        super(ListClasses, self).__init__()

    def handle_tracer_class(self, event):
        s = Structure(event[Parser.F_MESSAGE])
        print(s.name)

    def handle_tracer_entry(self, event):
        raise StopIteration


def format_ts(ts):
    sec = 1e9
    h = int(ts // (sec * 60 * 60))
    m = int((ts // (sec * 60)) % 60)
    s = (ts / sec)
    return '{:02d}.{:02d}.{:010.7f}'.format(h,m,s)

def is_time_field(f):
    # TODO: need proper units
    return (f.endswith('/time') or f.endswith('-dts') or f.endswith('-pts') or
        f.endswith('-duration'))

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('file', nargs='?', default='debug.log')
    parser.add_argument('-c', '--class', action='append', dest='classes',
                        help='tracer class selector (default: all)')
    parser.add_argument('-l', '--list-classes', action='store_true',
                        help='show tracer classes')
    args = parser.parse_args()

    analyzer = None
    if args.list_classes:
        analyzer = ListClasses()
    else:
        analyzer = stats = Stats(args.classes)

    with Parser(args.file) as log:
        runner = AnalysisRunner(log)
        runner.add_analyzer(analyzer)
        runner.run()

    if not args.list_classes:
        stats.report()
