try:
    from tracer.parser import Parser
except BaseException:
    from parser import Parser


class AnalysisRunner(object):
    """
    Runs several Analyzers over a log.

    Iterates log using a Parser and dispatches to a set of analyzers.
    """

    def __init__(self, log):
        self.log = log
        self.analyzers = []

    def add_analyzer(self, analyzer):
        self.analyzers.append(analyzer)

    def handle_tracer_class(self, event):
        for analyzer in self.analyzers:
            analyzer.handle_tracer_class(event)

    def handle_tracer_entry(self, event):
        for analyzer in self.analyzers:
            analyzer.handle_tracer_entry(event)

    def is_tracer_class(self, event):
        return (event[Parser.F_FILENAME] == 'gsttracerrecord.c'
                and event[Parser.F_CATEGORY] == 'GST_TRACER'
                and '.class' in event[Parser.F_MESSAGE])

    def is_tracer_entry(self, event):
        return (not event[Parser.F_LINE] and not event[Parser.F_FILENAME])

    def run(self):
        try:
            for event in self.log:
                # check if it is a tracer.class or tracer event
                if self.is_tracer_entry(event):
                    self.handle_tracer_entry(event)
                elif self.is_tracer_class(event):
                    self.handle_tracer_class(event)
                # else:
                #    print("unhandled:", repr(event))
        except StopIteration:
            pass
