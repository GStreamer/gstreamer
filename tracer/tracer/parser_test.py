import sys
import unittest

from tracer.parser import Parser

TESTFILE = './logs/trace.latency.log'

TEXT_DATA = ['first line', 'second line']

TRACER_LOG_DATA = [
    '0:00:00.079422574  7664      0x238ac70 TRACE             GST_TRACER :0:: thread-rusage, thread-id=(guint64)37268592, ts=(guint64)79416000, average-cpuload=(uint)1000, current-cpuload=(uint)1000, time=(guint64)79418045;'
]
TRACER_CLASS_LOG_DATA = [
    '0:00:00.041536066  1788      0x14b2150 TRACE             GST_TRACER gsttracerrecord.c:110:gst_tracer_record_build_format: latency.class, src=(structure)"scope\,\ type\=\(type\)gchararray\,\ related-to\=\(GstTracerValueScope\)GST_TRACER_VALUE_SCOPE_PAD\;", sink=(structure)"scope\,\ type\=\(type\)gchararray\,\ related-to\=\(GstTracerValueScope\)GST_TRACER_VALUE_SCOPE_PAD\;", time=(structure)"value\,\ type\=\(type\)guint64\,\ description\=\(string\)\"time\\\ it\\\ took\\\ for\\\ the\\\ buffer\\\ to\\\ go\\\ from\\\ src\\\ to\\\ sink\\\ ns\"\,\ flags\=\(GstTracerValueFlags\)GST_TRACER_VALUE_FLAGS_AGGREGATED\,\ min\=\(guint64\)0\,\ max\=\(guint64\)18446744073709551615\;";'
]


class TestParser(unittest.TestCase):

    def test___init__(self):
        log = Parser(TESTFILE)
        self.assertIsNone(log.file)

    def test___enter___with_file(self):
        with Parser(TESTFILE) as log:
            self.assertIsNotNone(log.file)

    def test___enter___with_stdin(self):
        sys.stdin = iter(TEXT_DATA)
        with Parser('-') as log:
            self.assertIsNotNone(log.file)

    def test_random_text_reports_none(self):
        sys.stdin = iter(TEXT_DATA)
        with Parser('-') as log:
            with self.assertRaises(StopIteration):
                next(log)

    def test_log_file_reports_trace_log(self):
        with Parser(TESTFILE) as log:
            self.assertIsNotNone(next(log))

    def test_trace_log_parsed(self):
        sys.stdin = iter(TRACER_LOG_DATA)
        with Parser('-') as log:
            event = next(log)
            self.assertEqual(len(event), 10)
