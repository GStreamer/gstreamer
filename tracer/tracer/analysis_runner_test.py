import unittest

from tracer.analysis_runner import AnalysisRunner

TRACER_CLASS = (
    '0:00:00.036373170', 1788, '0x23bca70', 'TRACE', 'GST_TRACER',
    'gsttracerrecord.c', 110, 'gst_tracer_record_build_format', None,
    r'latency.class, src=(structure)"scope\\,\\ type\\=\\(type\\)gchararray\\,\\ related-to\\=\\(GstTracerValueScope\\)GST_TRACER_VALUE_SCOPE_PAD\\;", sink=(structure)"scope\\,\\ type\\=\\(type\\)gchararray\\,\\ related-to\\=\\(GstTracerValueScope\\)GST_TRACER_VALUE_SCOPE_PAD\\;", time=(structure)"value\\,\\ type\\=\\(type\\)guint64\\,\\ description\\=\\(string\\)\\"time\\\\\\ it\\\\\\ took\\\\\\ for\\\\\\ the\\\\\\ buffer\\\\\\ to\\\\\\ go\\\\\\ from\\\\\\ src\\\\\\ to\\\\\\ sink\\\\\\ ns\\"\\,\\ flags\\=\\(GstTracerValueFlags\\)GST_TRACER_VALUE_FLAGS_AGGREGATED\\,\\ min\\=\\(guint64\\)0\\,\\ max\\=\\(guint64\\)18446744073709551615\\;";'
)

TRACER_ENTRY = (
    '0:00:00.142391137', 1788, '0x7f8a201056d0', 'TRACE', 'GST_TRACER',
    '', 0, '', None,
    r'latency, src=(string)source_src, sink=(string)pulsesink0_sink, time=(guint64)47091349;'
)


class TestAnalysisRunner(unittest.TestCase):

    def test_detect_tracer_class(self):
        a = AnalysisRunner(None)
        self.assertTrue(a.is_tracer_class(TRACER_CLASS))

    def test_detect_tracer_entry(self):
        a = AnalysisRunner(None)
        self.assertTrue(a.is_tracer_entry(TRACER_ENTRY))
