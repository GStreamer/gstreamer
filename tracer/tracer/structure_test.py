import unittest

from tracer.structure import Structure

BAD_NAME = r'foo bar'
BAD_KEY = r'foo, bar'
BAD_TYPE1 = r'foo, bar=['
BAD_TYPE2 = r'foo, bar=(int'

EMPTY_STRUCTURE = r'foo;'

SINGLE_VALUE_STRUCTURE = r'foo, key=(string)"value";'
MISC_TYPES_STRUCTURE = r'foo, key1=(string)"value", key2=(int)5, key3=(boolean)true;'

NESTED_STRUCTURE = r'foo, nested=(structure)"bar\,\ key1\=\(int\)0\,\ key2\=\(int\)5\;";'

class TestStructure(unittest.TestCase):

    def test_handles_bad_name(self):
        structure = Structure(BAD_NAME)
        self.assertFalse(structure.valid)
        self.assertEquals(structure.pos, 0)

    def test_handles_bad_key(self):
        structure = Structure(BAD_KEY)
        self.assertFalse(structure.valid)
        self.assertEquals(structure.pos, 5)

    def test_handles_bad_type1(self):
        structure = Structure(BAD_TYPE1)
        self.assertFalse(structure.valid)
        self.assertEquals(structure.pos, 9)

    def test_handles_bad_type2(self):
        structure = Structure(BAD_TYPE2)
        self.assertFalse(structure.valid)
        self.assertEquals(structure.pos, 10)

    def test_parses_empty_structure(self):
        structure = Structure(EMPTY_STRUCTURE)
        self.assertTrue(structure.valid)

    def test_parses_name_in_empty_structure(self):
        structure = Structure(EMPTY_STRUCTURE)
        self.assertEquals(structure.name, 'foo')

    def test_parses_single_value_structure(self):
        structure = Structure(SINGLE_VALUE_STRUCTURE)
        self.assertTrue(structure.valid)

    def test_parses_name(self):
        structure = Structure(SINGLE_VALUE_STRUCTURE)
        self.assertEquals(structure.name, 'foo')

    def test_parses_key(self):
        structure = Structure(SINGLE_VALUE_STRUCTURE)
        self.assertIn('key', structure.types)
        self.assertIn('key', structure.values)

    def test_parses_type(self):
        structure = Structure(SINGLE_VALUE_STRUCTURE)
        self.assertEquals(structure.types['key'], 'string')

    def test_parses_string_value(self):
        structure = Structure(MISC_TYPES_STRUCTURE)
        self.assertEquals(structure.values['key1'], 'value')

    def test_parses_int_value(self):
        structure = Structure(MISC_TYPES_STRUCTURE)
        self.assertEquals(structure.values['key2'], 5)

    def test_parses_nested_structure(self):
        structure = Structure(NESTED_STRUCTURE)
        self.assertTrue(structure.valid)
        sub = structure.values['nested']
        self.assertTrue(sub.valid)

    def test_nested_structure_has_sub_structure(self):
        structure = Structure(NESTED_STRUCTURE)
        self.assertEquals(structure.types['nested'], 'structure')
        self.assertIsInstance(structure.values['nested'], Structure)
