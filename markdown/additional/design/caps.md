# Caps

Caps are lightweight refcounted objects describing media types. They are
composed of an array of `GstStructure` plus, optionally, a
`GstCapsFeatures` set for the `GstStructure`.

Caps are exposed on `GstPadTemplates` to describe all possible types a
given pad can handle. They are also stored in the registry along with a
description of the element.

Caps are exposed on the element pads via `CAPS` and `ACCEPT_CAPS` queries.

This function describes the possible types that the pad can handle or
produce ([negotiation](additional/design/negotiation.md)).

Various methods exist to work with the media types such as subtracting
or intersecting.

## Operations

### Fixating

Caps are fixed if they only contain a single structure and this
structure is fixed. A structure is fixed if none of its fields
is of an unfixed type, for example a range, list or array.

For fixating caps only the first structure is kept, as the order in
which they appear is meant to express their precedence.
Afterwards, each unfixed field of this structure is set to
the value that makes most sense for the media format by the element or
pad implementation and then every remaining unfixed field is set to an
arbitrary value that is a subset of the unfixed field’s values.

EMPTY caps are fixed caps and ANY caps are not. Caps with ANY caps
features are not fixed.

### Subset

One caps "A" is a subset of another caps "B" if for each structure in
"A" there exists a structure in "B" that is a superset of the structure
in "A".

A structure "a" is the subset of a structure "b" if it has the same
structure name, the same caps features and each field in "b" either does not
exist in "a", or the value of the field in "a" is a subset of the value of the
field in "b". "a" must not have additional fields that are not in "b". Fields
that are in "b" but not in "a" (aka, an empty field) are always a subset.
This is different to the intuitive mathematical definition as an empty field
is defined to contain all possible values. This means that the empty field is
always a superset of any other field.

`EMPTY` caps are a subset of every other caps. Every caps are a subset of
`ANY` caps.

### Equality

Caps "A" and "B" are equal if "A" is a subset of "B" and "B" is a subset
of "A". This means that both caps are expressing the same possibilities
but their structures can still be different if they contain unfixed
fields.

### Intersection

The intersection of caps "A" and caps "B" are the caps that contain the
intersection of all their structures with each other.

The intersection of structure "a" and structure "b" is empty if their
structure name or their caps features are not equal, or if "a" and "b"
contain the same field but the intersection of both field values is
empty. If one structure contains a field that is not existing in the
other structure it will be copied over to the intersection with the same
value.

The intersection with `ANY` caps is always the other caps and the
intersection with `EMPTY` caps is always `EMPTY`.

### Union

The union of caps "A" and caps "B" are the caps that contain the union
of all their structures with each other.

The union of structure "a" and structure "b" are the two structures "a"
and "b" if the structure names or caps features are not equal.
Otherwise, the union is the structure that contains the union of each
fields value. If a field is only in one of the two structures it is not
contained in the union.

The union with ANY caps is always ANY and the union with EMPTY caps is
always the other caps.

### Subtraction

The subtraction of caps "A" from caps "B" is the most generic subset of
"B" that has an empty intersection with "A" but only contains structures
with names and caps features that are existing in "B".

## Basic Rules

### Semantics of caps and their usage

A caps can contain multiple structures, in which case any of the
structures would be acceptable. The structures are in the preferred
order of the creator of the caps, with the preferred structure being
first and during negotiation of caps this order should be considered to
select the most optimal structure.

Each of these structures has a name that specifies the media type, e.g.
"video/x-theora" to specify Theora video. Additional fields in the
structure add additional constraints and/or information about the media
type, like the width and height of a video frame, or the codec profile
that is used. These fields can be non-fixed (e.g. ranges) for non-fixed
caps but must be fixated to a fixed value during negotiation. If a field
is included in the caps returned by a pad via the `CAPS` query, it imposes
an additional constraint during negotiation. The caps in the end must
have this field with a value that is a subset of the non-fixed value.
Additional fields that are added in the negotiated caps give additional
information about the media but are treated as optional. Information
that can change for every buffer and is not relevant during negotiation
must not be stored inside the caps.

For each of the structures in caps it is possible to store caps
features. The caps features are expressing additional requirements for a
specific structure, and only structures with the same name *and* equal
caps features are considered compatible. Caps features can be used to
require a specific memory representation or a specific meta to be set on
buffers, for example a pad could require for a specific structure that
it is passed `EGLImage` memory or buffers with the video meta. If no caps
features are provided for a structure, it is assumed that system memory
is required unless later negotiation steps (e.g. the `ALLOCATION` query)
detect that something else can be used. The special `ANY` caps features
can be used to specify that any caps feature would be accepted, for
example if the buffer memory is not touched at all.

### Compatibility of caps

Pads can be linked when the caps of both pads are compatible. This is
the case when their intersection is not empty.

For checking if a pad actually supports a fixed caps an intersection is
not enough. Instead the fixed caps must be at least a subset of the
pad’s caps but pads can introduce additional constraints which would
be checked in the `ACCEPT_CAPS` query handler.

Data flow can only happen after pads have decided on common fixed caps.
These caps are distributed to both pads with the `CAPS` event.
