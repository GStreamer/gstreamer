#!/usr/bin/env python3
'''
How to run:
Offline:
GST_DEBUG="GST_TRACER:7" GST_TRACERS="stats;rusage;latency" GST_DEBUG_FILE=trace.log <application>
python3 gsttr-stats.py trace.log
'''

# TODO:
# options
# - list what is in the log
# - select which values to extract
# - live-update interval (for file=='-')

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

    def __init__(self):
        super(Stats, self).__init__()
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
            # TODO: parse params in event[Parser.F_MESSAGE]
            entry_name = event[Parser.F_FUNCTION]
        else:
            s = Structure(event[Parser.F_MESSAGE])
            entry_name = s.name
            record = self.records.get(entry_name)
            if record:
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
                        # TODO: skip 64bit -1 values ?
                        data['num'] += 1
                        data['sum'] += dv
                        if 'min' in data:
                            data['min'] = min(dv, data['min'])
                        if 'max' in data:
                            data['max'] = max(dv, data['max'])

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
    args = parser.parse_args()

    with Parser(args.file) as log:
        stats = Stats()
        runner = AnalysisRunner(log)
        runner.add_analyzer(stats)
        runner.run()

    # iterate scopes
    for sk,sv in stats.data.items():
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
            print("%-45s: Min/Avg,Max %30s: %s, %s, %s" % (sk, tk, mi, avg, ma))
