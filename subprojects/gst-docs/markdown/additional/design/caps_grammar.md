# GStreamer Caps/Structure String Syntax

## Overview

This grammar defines the canonical representation of media type capabilities and properties in GStreamer.

## Goals
- UTF-8 clean
- Simple string representation for most use cases
- Forward-parsed grammar
- Robust string escaping

## Grammar Specification

### CAPS
```
CAPS = STRUCTURE [ ';' STRUCTURE ]* [';']
```
- Fixed type

### STRUCTURE
```
STRUCTURE = STRUCTURE_NAME [ ',' FIELD ]*
```
- Fixed type

### STRUCTURE_NAME
```
STRUCTURE_NAME = STRING
```

### STRUCTURE FIELD
```
FIELD = FIELD_NAME '=' TYPED_VALUE
```
- Fixed type

### FIELD_NAME
```
FIELD_NAME = SIMPLE_STRING
```

### TYPED_VALUE
```
TYPED_VALUE = SIMPLE_VALUE
            | LIST
            | UNIQUE_LIST
            | ARRAY
            | RANGE
            | STRUCTURE
            | QUALIFIED_VALUE
```

### SIMPLE_VALUE
```
SIMPLE_VALUE = AUTO_VALUE

AUTO_VALUE = [-+]?[0-9][0-9]*
           | [-+]?[0-9][0-9]*[.][0-9]*[eE][-+][0-9]*
           | STRING
           | FRACTION
```

- AUTO_VALUE type is inferred from the value itself.
  - list of auto-value: int, float, string, fraction
- Fixed type

### LIST
```
LIST = '{' TYPED_VALUE [ ',' TYPED_VALUE ]* '}'
```
- Mixed types allowed
- Unordered
- No type constraints
- *Not* a fixed type

### UNIQUE_LIST
This container type is more commonly known as a 'set' but due limitation of the
introspection it's named 'unique list'.

```
UNIQUE_LIST = '(/uniquelist)' '{' TYPED_VALUE [ ',' TYPED_VALUE ]* '}'
    | '(' TYPE '/uniquelist)' '{' TYPE TYPED_VALUE [ ',' TYPE TYPED_VALUE ]* '}'
```

- Mixed types allowed
- Unordered
- No type constraints
- Fixed type
- Ensures uniqueness

### ARRAY
```
ARRAY = '<' TYPE TYPED_VALUE [ ',' TYPE TYPED_VALUE ]* '>'
```
- Fixed type
- Ordered

### RANGE
```
   RANGE = '[' RANGE_MIN ',' RANGE_MAX [ ',' RANGE_STEP ] ']'
```
- Represents inclusive range
- *Not* a fixed type
- RANGE_MIN and RANGE_MAX must have the same type
- RANGE_MIN must be inferior to RANGE_MAX
- RANGE_STEP is optional and default step value is 1 and only supported integer
  type value.

### RANGE_MIN
```
RANGE_MIN = RANGE_VALUE
```

### RANGE_MAX
```
RANGE_MAX = RANGE_VALUE
```

### RANGE_STEP
```
RANGE_STEP = '(int) VALUE
           | '(int64) VALUE'
```

### RANGE_VALUE
```
RANGE_VALUE  = '(int)' VALUE
            | '(int64)' VALUE
            | '(double)' VALUE
            | '(fraction)' VALUE
            | [-+]?[0-9][0-9]*
            | [-+]?[0-9][0-9]*[.][0-9]*[eE][-+][0-9]*
```

### QUALIFIED_VALUE
```
QUALIFIED_VALUE = '(' TYPE ')' VALUE
                | '(' DEFAULT_TYPE ')' LIST
                | '(' DEFAULT_TYPE ')' ARRAY
                | '(' DEFAULT_TYPE ')' RANGE
                | '(' DEFAULT_TYPE ')' STRUCTURE
                | '(' DEFAULT_TYPE / "uniquelist" ')' UNIQUE_LIST
```
QUALIFIED_VALUE is a value where the type is explicitly specified.

### DEFAULT_TYPE

```
DEFAULT_TYPE = TYPE
```
 - Represents the explicit type annotation for container's elements

### VALUE
```
VALUE = STRING
```
- Fixed type

### STRING
```
STRING = ["][^"]["]
       | ['][^'][']
       | SIMPLE_STRING
```
- Fixed type

### SIMPLE_STRING
```
SIMPLE_STRING = [A-Za-z0-9_+-:./]+
```

### FRACTION
```
FRACTION = '(' fraction ')' NUMERATOR '/' DENOMINATOR
NUMERATOR = [0-9][0-9]*
DENOMINATOR = [0-9][0-9]*
```
- Fixed type

### TYPE
```
TYPE =  "buffer"
      | "fraction"
      | "structure"
      | "datetime"
      | "bitmask"
      | "flagset"
      | "sample"
      | "taglist"
      | "array"
      | "list"
      | "set"
      | "caps"
      | GTYPE
```

### GTYPE
```
GTYPE = "int" | "i"
      | "uint" | "u"
      | "float" | "f"
      | "double" | "d"
      | "boolean" | "bool" | "b"
      | "string" | "str" | "s"
      | "date"
      | "gdatetime"
      | "type"
```


## Summary Table
| Element | Mixed Types | Ordered | Unique | Fixed Type |
|---------|-------------|---------|--------|-----------|
| **LIST** | Yes | No | No | No |
| **UNIQUE_LIST** | Conditional | No | Yes | Yes* |
| **ARRAY** | No | Yes | No | Yes* |
| **RANGE** | No | N/A | N/A | No |
| **STRUCTURE** | Yes | N/A | N/A | Yes* |
| **AUTO_VALUE** | N/A | N/A | N/A | mixed |
| **FRACTION** | N/A | N/A | N/A | Yes |

**Legend:**

(*) = Fixed type when all its elements are fixed (or when explicitly typed)

## Canonical Examples
# Basic structures
```
"audio/raw"
"audio/raw, rate=(int)44100"
"audio/raw, rate=(int)44100, signed=(boolean)true"

```
# Lists
```
"l-structure, l-field={ (int)1, (float)2.1 }"
"l-structure, l-field=(int){ 1, 2 }"
```

# UniqueLists
```
"s-structure, s-field=(/uniquelist){ (int)1, (float)2.1 }"
"s-structure, s-field=(int/uniquelist){ 1, 2, 4 }"
"s-structure, s-field=(/uniquelist){ 'a', 'b', 'c' }"
```

# Arrays
```
"a-structure, a-field=< (int)1, (float)2.1>"
"a-structure", a-field=(float)< 1.0, 2.1>"
```

# Ranges
```
"r-structure, r-field=[1, 5]"
"r-structure, r-field=[1, 5, 2]"
```

# Complex example
```
"c-structure,
    l-field=(int){ 1, 2 },
    s-field=(int/uniquelist){ 1, 2, 4 },
    a-field=(float)< 1.0, 2.1>,
    r-field=[1, 5],
    inner-struc=(structure)[sub-struct, ss-field=1]"
```

