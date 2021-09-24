class Analyzer(object):
    """
    Base class for a gst tracer analyzer.

    Will be used in conjunction with a AnalysisRunner.
    """

    def __init__(self):
        pass

    def handle_tracer_class(self, event):
        pass

    def handle_tracer_entry(self, event):
        pass
