import logging
import unittest

from tracer.structure import Structure

logging.basicConfig(level=logging.INFO)

BAD_NAME = r'foo bar'
BAD_KEY = r'foo, bar'
BAD_TYPE1 = r'foo, bar=['
BAD_TYPE2 = r'foo, bar=(int'

EMPTY_STRUCTURE = r'foo;'

SINGLE_VALUE_STRUCTURE = r'foo, key=(string)"value";'
MISC_TYPES_STRUCTURE = r'foo, key1=(string)"value", key2=(int)5, key3=(boolean)1;'

NESTED_STRUCTURE = r'foo, nested=(structure)"bar\,\ key1\=\(int\)0\,\ key2\=\(int\)5\;";'

REGRESSIONS = [
    r'query, thread-id=(guint64)139839438879824, ts=(guint64)220860464, pad-ix=(uint)8, element-ix=(uint)9, peer-pad-ix=(uint)9, peer-element-ix=(uint)8, name=(string)accept-caps, structure=(structure)"GstQueryAcceptCaps\,\ caps\=\(GstCaps\)\"audio/mpeg\\\,\\\ mpegversion\\\=\\\(int\\\)4\\\,\\\ framed\\\=\\\(boolean\\\)true\\\,\\\ stream-format\\\=\\\(string\\\)raw\\\,\\\ level\\\=\\\(string\\\)2\\\,\\\ base-profile\\\=\\\(string\\\)lc\\\,\\\ profile\\\=\\\(string\\\)lc\\\,\\\ codec_data\\\=\\\(buffer\\\)1210\\\,\\\ rate\\\=\\\(int\\\)44100\\\,\\\ channels\\\=\\\(int\\\)2\"\,\ result\=\(boolean\)false\;", have-res=(boolean)0, res=(boolean)0;',
    r'message, thread-id=(guint64)139838900680560, ts=(guint64)1000451258, element-ix=(uint)2, name=(string)tag, structure=(structure)"GstMessageTag\,\ taglist\=\(taglist\)\"taglist\\\,\\\ datetime\\\=\\\(datetime\\\)2009-03-05T12:57:08Z\\\,\\\ private-qt-tag\\\=\\\(sample\\\)\\\{\\\ 00000019677373740000001164617461000000010000000030:None:R3N0U2VnbWVudCwgZmxhZ3M9KEdzdFNlZ21lbnRGbGFncylHU1RfU0VHTUVOVF9GTEFHX05PTkUsIHJhdGU9KGRvdWJsZSkxLCBhcHBsaWVkLXJhdGU9KGRvdWJsZSkxLCBmb3JtYXQ9KEdzdEZvcm1hdClHU1RfRk9STUFUX1RJTUUsIGJhc2U9KGd1aW50NjQpMCwgb2Zmc2V0PShndWludDY0KTAsIHN0YXJ0PShndWludDY0KTAsIHN0b3A9KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTUsIHRpbWU9KGd1aW50NjQpMCwgcG9zaXRpb249KGd1aW50NjQpMCwgZHVyYXRpb249KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTU7AA__:YXBwbGljYXRpb24veC1nc3QtcXQtZ3NzdC10YWcsIHN0eWxlPShzdHJpbmcpaXR1bmVzOwA_\\\,\\\ 0000001e6773746400000016646174610000000100000000313335353130:None:R3N0U2VnbWVudCwgZmxhZ3M9KEdzdFNlZ21lbnRGbGFncylHU1RfU0VHTUVOVF9GTEFHX05PTkUsIHJhdGU9KGRvdWJsZSkxLCBhcHBsaWVkLXJhdGU9KGRvdWJsZSkxLCBmb3JtYXQ9KEdzdEZvcm1hdClHU1RfRk9STUFUX1RJTUUsIGJhc2U9KGd1aW50NjQpMCwgb2Zmc2V0PShndWludDY0KTAsIHN0YXJ0PShndWludDY0KTAsIHN0b3A9KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTUsIHRpbWU9KGd1aW50NjQpMCwgcG9zaXRpb249KGd1aW50NjQpMCwgZHVyYXRpb249KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTU7AA__:YXBwbGljYXRpb24veC1nc3QtcXQtZ3N0ZC10YWcsIHN0eWxlPShzdHJpbmcpaXR1bmVzOwA_\\\,\\\ 0000003867737364000000306461746100000001000000004244354241453530354d4d313239353033343539373733353435370000000000:None:R3N0U2VnbWVudCwgZmxhZ3M9KEdzdFNlZ21lbnRGbGFncylHU1RfU0VHTUVOVF9GTEFHX05PTkUsIHJhdGU9KGRvdWJsZSkxLCBhcHBsaWVkLXJhdGU9KGRvdWJsZSkxLCBmb3JtYXQ9KEdzdEZvcm1hdClHU1RfRk9STUFUX1RJTUUsIGJhc2U9KGd1aW50NjQpMCwgb2Zmc2V0PShndWludDY0KTAsIHN0YXJ0PShndWludDY0KTAsIHN0b3A9KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTUsIHRpbWU9KGd1aW50NjQpMCwgcG9zaXRpb249KGd1aW50NjQpMCwgZHVyYXRpb249KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTU7AA__:YXBwbGljYXRpb24veC1nc3QtcXQtZ3NzZC10YWcsIHN0eWxlPShzdHJpbmcpaXR1bmVzOwA_\\\,\\\ 0000009867737075000000906461746100000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000:None:R3N0U2VnbWVudCwgZmxhZ3M9KEdzdFNlZ21lbnRGbGFncylHU1RfU0VHTUVOVF9GTEFHX05PTkUsIHJhdGU9KGRvdWJsZSkxLCBhcHBsaWVkLXJhdGU9KGRvdWJsZSkxLCBmb3JtYXQ9KEdzdEZvcm1hdClHU1RfRk9STUFUX1RJTUUsIGJhc2U9KGd1aW50NjQpMCwgb2Zmc2V0PShndWludDY0KTAsIHN0YXJ0PShndWludDY0KTAsIHN0b3A9KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTUsIHRpbWU9KGd1aW50NjQpMCwgcG9zaXRpb249KGd1aW50NjQpMCwgZHVyYXRpb249KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTU7AA__:YXBwbGljYXRpb24veC1nc3QtcXQtZ3NwdS10YWcsIHN0eWxlPShzdHJpbmcpaXR1bmVzOwA_\\\,\\\ 000000986773706d000000906461746100000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000:None:R3N0U2VnbWVudCwgZmxhZ3M9KEdzdFNlZ21lbnRGbGFncylHU1RfU0VHTUVOVF9GTEFHX05PTkUsIHJhdGU9KGRvdWJsZSkxLCBhcHBsaWVkLXJhdGU9KGRvdWJsZSkxLCBmb3JtYXQ9KEdzdEZvcm1hdClHU1RfRk9STUFUX1RJTUUsIGJhc2U9KGd1aW50NjQpMCwgb2Zmc2V0PShndWludDY0KTAsIHN0YXJ0PShndWludDY0KTAsIHN0b3A9KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTUsIHRpbWU9KGd1aW50NjQpMCwgcG9zaXRpb249KGd1aW50NjQpMCwgZHVyYXRpb249KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTU7AA__:YXBwbGljYXRpb24veC1nc3QtcXQtZ3NwbS10YWcsIHN0eWxlPShzdHJpbmcpaXR1bmVzOwA_\\\,\\\ 0000011867736868000001106461746100000001000000007631302e6c736361636865332e632e796f75747562652e636f6d0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000:None:R3N0U2VnbWVudCwgZmxhZ3M9KEdzdFNlZ21lbnRGbGFncylHU1RfU0VHTUVOVF9GTEFHX05PTkUsIHJhdGU9KGRvdWJsZSkxLCBhcHBsaWVkLXJhdGU9KGRvdWJsZSkxLCBmb3JtYXQ9KEdzdEZvcm1hdClHU1RfRk9STUFUX1RJTUUsIGJhc2U9KGd1aW50NjQpMCwgb2Zmc2V0PShndWludDY0KTAsIHN0YXJ0PShndWludDY0KTAsIHN0b3A9KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTUsIHRpbWU9KGd1aW50NjQpMCwgcG9zaXRpb249KGd1aW50NjQpMCwgZHVyYXRpb249KGd1aW50NjQpMTg0NDY3NDQwNzM3MDk1NTE2MTU7AA__:YXBwbGljYXRpb24veC1nc3QtcXQtZ3NoaC10YWcsIHN0eWxlPShzdHJpbmcpaXR1bmVzOwA_\\\ \\\}\\\,\\\ container-format\\\=\\\(string\\\)\\\"ISO\\\\\\\ MP4/M4A\\\"\\\;\"\;";',
]


class TestStructure(unittest.TestCase):

    def test_handles_bad_name(self):
        structure = None
        with self.assertRaises(ValueError):
            structure = Structure(BAD_NAME)

    def test_handles_bad_key(self):
        structure = None
        with self.assertRaises(ValueError):
            structure = Structure(BAD_KEY)

    def test_handles_bad_type1(self):
        structure = None
        with self.assertRaises(ValueError):
            structure = Structure(BAD_TYPE1)

    def test_handles_bad_type2(self):
        structure = None
        with self.assertRaises(ValueError):
            structure = Structure(BAD_TYPE2)

    def test_parses_empty_structure(self):
        structure = Structure(EMPTY_STRUCTURE)
        self.assertEqual(structure.text, EMPTY_STRUCTURE)

    def test_parses_name_in_empty_structure(self):
        structure = Structure(EMPTY_STRUCTURE)
        self.assertEqual(structure.name, 'foo')

    def test_parses_single_value_structure(self):
        structure = Structure(SINGLE_VALUE_STRUCTURE)
        self.assertEqual(structure.text, SINGLE_VALUE_STRUCTURE)

    def test_parses_name(self):
        structure = Structure(SINGLE_VALUE_STRUCTURE)
        self.assertEqual(structure.name, 'foo')

    def test_parses_key(self):
        structure = Structure(SINGLE_VALUE_STRUCTURE)
        self.assertIn('key', structure.types)
        self.assertIn('key', structure.values)

    def test_parses_type(self):
        structure = Structure(SINGLE_VALUE_STRUCTURE)
        self.assertEqual(structure.types['key'], 'string')

    def test_parses_string_value(self):
        structure = Structure(MISC_TYPES_STRUCTURE)
        self.assertEqual(structure.values['key1'], 'value')

    def test_parses_int_value(self):
        structure = Structure(MISC_TYPES_STRUCTURE)
        self.assertEqual(structure.values['key2'], 5)

    def test_parses_boolean_value(self):
        structure = Structure(MISC_TYPES_STRUCTURE)
        self.assertEqual(structure.values['key3'], True)

    def test_parses_nested_structure(self):
        structure = Structure(NESTED_STRUCTURE)
        self.assertEqual(structure.text, NESTED_STRUCTURE)

    def test_nested_structure_has_sub_structure(self):
        structure = Structure(NESTED_STRUCTURE)
        self.assertEqual(structure.types['nested'], 'structure')
        self.assertIsInstance(structure.values['nested'], Structure)

    def test_regressions(self):
        for s in REGRESSIONS:
            structure = Structure(s)
