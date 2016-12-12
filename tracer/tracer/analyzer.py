import re
import sys

from tracer.parser import Parser
from tracer.structure import Structure

_SCOPE_RELATED_TO = {
    'GST_TRACER_VALUE_SCOPE_PAD': 'Pad',
    'GST_TRACER_VALUE_SCOPE_ELEMENT': 'Element',
    'GST_TRACER_VALUE_SCOPE_THREAD': 'Thread',
    'GST_TRACER_VALUE_SCOPE_PROCESS': 'Process',
}

_NUMERIC_TYPES = ('int', 'uint', 'gint', 'guint', 'gint64', 'guint64')

class Analyzer(object):
    '''Base class for a gst tracer analyzer.'''

    def __init__(self, log):
        self.log = log
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
            vmethod_name = event[Parser.F_FUNCTION]
        else:
            s = Structure(event[Parser.F_MESSAGE])
            vmethod_name = s.name
            record = self.records.get(vmethod_name)
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
                        key = vmethod_name + "/" + vk
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

        # TODO: check if self has a catch-all handler and call first (check this in init)
        # - we can use this to chain filters, allthough the chained filter
        #   would be doing the same as below
        # check if self['vmethod'] is a function, if so call
        vmethod = getattr (self, vmethod_name, None)
        if callable(vmethod):
            vmethod (event)

    def is_tracer_class(self, event):
        return (event[Parser.F_FILENAME] == 'gsttracerrecord.c' and
                    event[Parser.F_CATEGORY] == 'GST_TRACER' and
                    '.class' in event[Parser.F_MESSAGE])

    def is_tracer_entry(self, event):
        return (not event[Parser.F_LINE] and not event[Parser.F_FILENAME])

    def run(self):
        for event in self.log:
            # check if it is a tracer.class or tracer event
            if self.is_tracer_class(event):
                self.handle_tracer_class(event)
            elif self.is_tracer_entry(event):
                self.handle_tracer_entry(event)
            #else:
            #    print("unhandled:", repr(event))
