# GES action types

## seek


``` validate-scenario
seek,
    flags=(string describing the GstSeekFlags to set),
    start=(double or string (GstClockTime)),
    [rate=(double)],
    [start_type=(string)],
    [stop=(double or string (GstClockTime))],
    [stop_type=(string)],
    [playback-time=(double,string)];
```

Seeks into the stream. This is an example of a seek happening when the stream reaches 5 seconds
or 1 eighth of its duration and seeks to 10s or 2 eighths of its duration:


```
  seek, playback-time="min(5.0, (duration/8))", start="min(10, 2*(duration/8))", flags=accurate+flush
```


**Implementer namespace**: ges

### Parameters

#### `flags` (_mandatory_)

The GstSeekFlags to use

**Possible types**: `string describing the GstSeekFlags to set`

---

#### `start` (_mandatory_)

The starting value of the seek

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double or string (GstClockTime)`

---

#### `rate` (_optional_)

The rate value of the seek

**Possible types**: `double`

**Default**: 1.0

---

#### `start_type` (_optional_)

The GstSeekType to use for the start of the seek, in:
  [none, set, end]

**Possible types**: `string`

**Default**: set

---

#### `stop` (_optional_)

The stop value of the seek

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double or string (GstClockTime)`

**Default**: GST_CLOCK_TIME_NONE

---

#### `stop_type` (_optional_)

The GstSeekType to use for the stop of the seek, in:
  [none, set, end]

**Possible types**: `string`

**Default**: set

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## edit-container


``` validate-scenario
edit-container,
    container-name=(string),
    [edge=(string)],
    [edit-mode=(string)],
    [new-layer-priority=(int)],
    [position=(double or string)],
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Allows to edit a container (like a GESClip), for more details, have a look at:
ges_timeline_element_edit documentation, Note that the timeline will
be committed, and flushed so that the edition is taken into account

**Implementer namespace**: ges

### Parameters

#### `container-name` (_mandatory_)

The name of the GESContainer to edit

**Possible types**: `string`

---

#### `edge` (_optional_)

The GESEdge to use to edit @container-name
should be in [ start, end, none ] 

**Possible types**: `string`

**Default**: none

---

#### `edit-mode` (_optional_)

The GESEditMode to use to edit @container-name

**Possible types**: `string`

**Default**: normal

---

#### `new-layer-priority` (_optional_)

The priority of the layer @container should land in.
If the layer you're trying to move the container to doesn't exist, it will
be created automatically. -1 means no move.

**Possible types**: `int`

**Default**: -1

---

#### `position` (_optional_)

The new position of the GESContainer

**Possible variables**:

  * position: The current position in the stream

  * duration: The duration of the stream

**Possible types**: `double or string`

**Default**: (null)

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## edit


``` validate-scenario
edit,
    element-name=(string),
    [edge=(string)],
    [edit-mode=(string)],
    [new-layer-priority=(int)],
    [position=(double or string)],
    [project-uri=(string)],
    [source-frame=(double or string)],
    [playback-time=(double,string)];
```

Allows to edit a element (like a GESClip), for more details, have a look at:
ges_timeline_element_edit documentation, Note that the timeline will
be committed, and flushed so that the edition is taken into account

**Implementer namespace**: ges

### Parameters

#### `element-name` (_mandatory_)

The name of the element to edit

**Possible types**: `string`

---

#### `edge` (_optional_)

The GESEdge to use to edit @element-name
should be in [ start, end, none ] 

**Possible types**: `string`

**Default**: none

---

#### `edit-mode` (_optional_)

The GESEditMode to use to edit @element-name

**Possible types**: `string`

**Default**: normal

---

#### `new-layer-priority` (_optional_)

The priority of the layer @element should land in.
If the layer you're trying to move the element to doesn't exist, it will
be created automatically. -1 means no move.

**Possible types**: `int`

**Default**: -1

---

#### `position` (_optional_)

The new position of the element

**Possible variables**:

  * position: The current position in the stream

  * duration: The duration of the stream

**Possible types**: `double or string`

**Default**: (null)

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `source-frame` (_optional_)

The new frame of the element, computed from the @element-nameclip's source frame.

**Possible types**: `double or string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## add-asset


``` validate-scenario
add-asset,
    id,
    type,
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Allows to add an asset to the current project

**Implementer namespace**: ges

### Parameters

#### `id` (_mandatory_)

Adds an asset to a project.

---

#### `type` (_mandatory_)

The type of asset to add

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## remove-asset


``` validate-scenario
remove-asset,
    id,
    type,
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Allows to remove an asset from the current project

**Implementer namespace**: ges

### Parameters

#### `id` (_mandatory_)

The ID of the clip to remove

---

#### `type` (_mandatory_)

The type of asset to remove

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## add-layer


``` validate-scenario
add-layer,
    [priority],
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Allows to add a layer to the current timeline

**Implementer namespace**: ges

### Parameters

#### `priority` (_optional_)

The priority of the new layer to add,if not specified, the new layer will be appended to the timeline

**Default**: (null)

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## remove-layer


``` validate-scenario
remove-layer,
    priority,
    [auto-transition=(boolean)],
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Allows to remove a layer from the current timeline

**Implementer namespace**: ges

### Parameters

#### `priority` (_mandatory_)

The priority of the layer to remove

---

#### `auto-transition` (_optional_)

Whether auto-transition is activated on the new layer.

**Possible types**: `boolean`

**Default**: False

---

#### `project-uri` (_optional_)

The nested timeline to add clip to

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## add-clip


``` validate-scenario
add-clip,
    asset-id=(string),
    layer-priority=(int),
    name=(string),
    type=(string),
    [duration=(double or string)],
    [inpoint=(double or string)],
    [project-uri=(string)],
    [start=(double or string)],
    [playback-time=(double,string)];
```

Allows to add a clip to a given layer

**Implementer namespace**: ges

### Parameters

#### `asset-id` (_mandatory_)

The id of the asset from which to extract the clip

**Possible types**: `string`

---

#### `layer-priority` (_mandatory_)

The priority of the clip to add

**Possible types**: `int`

---

#### `name` (_mandatory_)

The name of the clip to add

**Possible types**: `string`

---

#### `type` (_mandatory_)

The type of the clip to create

**Possible types**: `string`

---

#### `duration` (_optional_)

The  duration value to set on the new GESClip

**Possible types**: `double or string`

**Default**: (null)

---

#### `inpoint` (_optional_)

The  inpoint value to set on the new GESClip

**Possible types**: `double or string`

**Default**: (null)

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `start` (_optional_)

The start value to set on the new GESClip.

**Possible types**: `double or string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## remove-clip


``` validate-scenario
remove-clip,
    name=(string),
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Allows to remove a clip from a given layer

**Implementer namespace**: ges

### Parameters

#### `name` (_mandatory_)

The name of the clip to remove

**Possible types**: `string`

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## serialize-project


``` validate-scenario
serialize-project,
    uri=(string),
    [playback-time=(double,string)];
```

serializes a project

**Implementer namespace**: ges

### Parameters

#### `uri` (_mandatory_)

The uri where to store the serialized project

**Possible types**: `string`

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## set-child-property


``` validate-scenario
set-child-property,
    element-name=(string),
    property=(string),
    value=(gvalue),
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Allows to change child property of an object

**Implementer namespace**: ges

### Parameters

#### `element-name` (_mandatory_)

The name of the element on which to modify the property

**Possible types**: `string`

---

#### `property` (_mandatory_)

The name of the property to modify

**Possible types**: `string`

---

#### `value` (_mandatory_)

The value of the property

**Possible types**: `gvalue`

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## set-layer-active


``` validate-scenario
set-layer-active,
    active=(gboolean),
    layer-priority=(gint),
    [tracks=({string, })],
    [playback-time=(double,string)];
```

Set activness of a layer (on optional tracks).

**Implementer namespace**: ges

### Parameters

#### `active` (_mandatory_)

The activness of the layer

**Possible types**: `gboolean`

---

#### `layer-priority` (_mandatory_)

The priority of the layer to set activness on

**Possible types**: `gint`

---

#### `tracks` (_optional_)

tracks

**Possible types**: `{string, }`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## set-ges-properties


``` validate-scenario
set-ges-properties,
    element-name=(string),
    [playback-time=(double,string)];
```

Set `element-name` properties values defined by the fields in the following format: `property_name=expected-value`

**Implementer namespace**: ges

### Parameters

#### `element-name` (_mandatory_)

The name of the element on which to set properties

**Possible types**: `string`

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## check-ges-properties


``` validate-scenario
check-ges-properties,
    element-name=(string),
    [playback-time=(double,string)];
```

Check `element-name` properties values defined by the fields in the following format: `property_name=expected-value`

**Implementer namespace**: ges

### Parameters

#### `element-name` (_mandatory_)

The name of the element on which to check properties

**Possible types**: `string`

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## check-child-properties


``` validate-scenario
check-child-properties,
    element-name=(string),
    [at-time=(string)],
    [playback-time=(double,string)];
```

Check `element-name` children properties values defined by the fields in the following format: `property_name=expected-value`

**Implementer namespace**: ges

### Parameters

#### `element-name` (_mandatory_)

The name of the element on which to check children properties

**Possible types**: `string`

---

#### `at-time` (_optional_)

The time at which to check the values, taking into account the ControlBinding if any set.

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## set-child-properties


``` validate-scenario
set-child-properties,
    element-name=(string),
    [playback-time=(double,string)];
```

Sets `element-name` children properties values defined by the fields in the following format: `property-name=new-value`

**Implementer namespace**: ges

### Parameters

#### `element-name` (_mandatory_)

The name of the element on which to modify child properties

**Possible types**: `string`

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## split-clip


``` validate-scenario
split-clip,
    clip-name=(string),
    position=(double or string),
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Split a clip at a specified position.

**Implementer namespace**: ges

### Parameters

#### `clip-name` (_mandatory_)

The name of the clip to split

**Possible types**: `string`

---

#### `position` (_mandatory_)

The position at which to split the clip

**Possible types**: `double or string`

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## set-track-restriction-caps


``` validate-scenario
set-track-restriction-caps,
    caps=(string),
    track-type=(string),
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Sets restriction caps on tracks of a specific type.

**Implementer namespace**: ges

### Parameters

#### `caps` (_mandatory_)

The caps to set on the track

**Possible types**: `string`

---

#### `track-type` (_mandatory_)

The type of track to set restriction caps on

**Possible types**: `string`

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## element-set-asset


``` validate-scenario
element-set-asset,
    asset-id=(string),
    element-name=(string),
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Sets restriction caps on tracks of a specific type.

**Implementer namespace**: ges

### Parameters

#### `asset-id` (_mandatory_)

The id of the asset from which to extract the clip

**Possible types**: `string`

---

#### `element-name` (_mandatory_)

The name of the TimelineElement to set an asset on

**Possible types**: `string`

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## container-add-child


``` validate-scenario
container-add-child,
    asset-id=(string),
    container-name=(string),
    [child-name=(string)],
    [child-type=(string)],
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Add a child to @container-name. If asset-id and child-type are specified, the child will be created and added. Otherwise @child-name has to be specified and will be added to the container.

**Implementer namespace**: ges

### Parameters

#### `asset-id` (_mandatory_)

The id of the asset from which to extract the child

**Possible types**: `string`

---

#### `container-name` (_mandatory_)

The name of the GESContainer to add a child to

**Possible types**: `string`

---

#### `child-name` (_optional_)

The name of the child to add to @container-name

**Possible types**: `string`

**Default**: NULL

---

#### `child-type` (_optional_)

The type of the child to create

**Possible types**: `string`

**Default**: NULL

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## container-remove-child


``` validate-scenario
container-remove-child,
    child-name=(string),
    container-name=(string),
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Remove a child from @container-name.

**Implementer namespace**: ges

### Parameters

#### `child-name` (_mandatory_)

The name of the child to reomve from @container-name

**Possible types**: `string`

---

#### `container-name` (_mandatory_)

The name of the GESContainer to remove a child from

**Possible types**: `string`

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## group


``` validate-scenario
group,
    containers=({ container-name, }),
    [container-name=(string)],
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Group containers together.

**Implementer namespace**: ges

### Parameters

#### `containers` (_mandatory_)

Array of GESContainer names to group

**Possible types**: `{ container-name, }`

---

#### `container-name` (_optional_)

The name of the resulting group

**Possible types**: `string`

**Default**: (null)

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## ungroup-container


``` validate-scenario
ungroup-container,
    container-name=(string),
    [project-uri=(string)],
    [recursive=(boolean)],
    [playback-time=(double,string)];
```

Ungroup children of @container-name.

**Implementer namespace**: ges

### Parameters

#### `container-name` (_mandatory_)

The name of the GESContainer to ungroup children from

**Possible types**: `string`

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `recursive` (_optional_)

Whether to recurse ungrouping or not.

**Possible types**: `boolean`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## set-control-source


``` validate-scenario
set-control-source,
    element-name=(string),
    property-name=(string),
    [binding-type=(string)],
    [interpolation-mode=(string)],
    [project-uri=(string)],
    [source-type=(string)],
    [playback-time=(double,string)];
```

Adds a GstControlSource on @element-name::@property-name allowing you to then add keyframes on that property.

**Implementer namespace**: ges

### Parameters

#### `element-name` (_mandatory_)

The name of the GESTrackElement to set the control source on

**Possible types**: `string`

---

#### `property-name` (_mandatory_)

The name of the property for which to set a control source

**Possible types**: `string`

---

#### `binding-type` (_optional_)

The name of the type of binding to use

**Possible types**: `string`

**Default**: direct

---

#### `interpolation-mode` (_optional_)

The name of the GstInterpolationMode to on the source

**Possible types**: `string`

**Default**: linear

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `source-type` (_optional_)

The name of the type of ControlSource to use

**Possible types**: `string`

**Default**: interpolation

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## add-keyframe


``` validate-scenario
add-keyframe,
    element-name=(string),
    property-name=(string),
    timestamp=(string or float),
    value=(float),
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Set a keyframe on @element-name:property-name.

**Implementer namespace**: ges

### Parameters

#### `element-name` (_mandatory_)

The name of the GESTrackElement to add a keyframe on

**Possible types**: `string`

---

#### `property-name` (_mandatory_)

The name of the property for which to add a keyframe on

**Possible types**: `string`

---

#### `timestamp` (_mandatory_)

The timestamp of the keyframe

**Possible types**: `string or float`

---

#### `value` (_mandatory_)

The value of the keyframe

**Possible types**: `float`

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## copy-element


``` validate-scenario
copy-element,
    element-name=(string),
    position=(string or float),
    [paste-name=(string)],
    [project-uri=(string)],
    [recurse=(boolean)],
    [playback-time=(double,string)];
```

Remove a child from @container-name.

**Implementer namespace**: ges

### Parameters

#### `element-name` (_mandatory_)

The name of the GESTtimelineElement to copy

**Possible types**: `string`

---

#### `position` (_mandatory_)

The time where to paste the element

**Possible types**: `string or float`

---

#### `paste-name` (_optional_)

The name of the copied element

**Possible types**: `string`

**Default**: (null)

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `recurse` (_optional_)

Copy recursively or not

**Possible types**: `boolean`

**Default**: true

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## remove-keyframe


``` validate-scenario
remove-keyframe,
    element-name=(string),
    property-name=(string),
    timestamp=(string or float),
    [project-uri=(string)],
    [playback-time=(double,string)];
```

Remove a keyframe on @element-name:property-name.

**Implementer namespace**: ges

### Parameters

#### `element-name` (_mandatory_)

The name of the GESTrackElement to add a keyframe on

**Possible types**: `string`

---

#### `property-name` (_mandatory_)

The name of the property for which to add a keyframe on

**Possible types**: `string`

---

#### `timestamp` (_mandatory_)

The timestamp of the keyframe

**Possible types**: `string or float`

---

#### `project-uri` (_optional_)

The project URI with the serialized timeline to execute the action on

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## load-project


``` validate-scenario
load-project,
    [serialized-content=(string)],
    [uri=(string)],
    [playback-time=(double,string)];
```

Loads a project either from its content passed in the 'serialized-content' field or using the provided 'uri'.
Note that it will completely clean the previous timeline

**Implementer namespace**: ges

### Parameters

#### `serialized-content` (_optional_)

The full content of the XML describing project in XGES format.

**Possible types**: `string`

**Default**: (null)

---

#### `uri` (_optional_)

The uri of the project to load (used only if serialized-content is not provided)

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## commit


``` validate-scenario
commit,
    [playback-time=(double,string)];
```

Commit the timeline.

**Implementer namespace**: ges

### Parameters

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---

## check-ges-video-element-selector


``` validate-scenario
check-ges-video-element-selector,
    [colorconvert-factory=(string)],
    [compositor-factory=(string)],
    [convert-scale-factory=(string)],
    [deinterlace-factory=(string)],
    [downloader-factory=(string)],
    [memory-feature=(string)],
    [scale-factory=(string)],
    [strict=(boolean)],
    [uploader-factory=(string)],
    [videoflip-factory=(string)],
    [playback-time=(double,string)];
```

Assert that the GESVideoElementSelector singleton resolved to the expected factories and flags. Every field is optional; fields not listed in the action are not checked.

**Implementer namespace**: ges

### Parameters

#### `colorconvert-factory` (_optional_)

Expected colorconvert factory name.

**Possible types**: `string`

**Default**: (null)

---

#### `compositor-factory` (_optional_)

Expected name of the selected compositor factory.

**Possible types**: `string`

**Default**: (null)

---

#### `convert-scale-factory` (_optional_)

Expected combined convert+scale factory name. Empty string asserts none (fallback sandwich is used).

**Possible types**: `string`

**Default**: (null)

---

#### `deinterlace-factory` (_optional_)

Expected deinterlace factory name.

**Possible types**: `string`

**Default**: (null)

---

#### `downloader-factory` (_optional_)

Expected downloader factory name. Empty string asserts none.

**Possible types**: `string`

**Default**: (null)

---

#### `memory-feature` (_optional_)

Expected memory feature (e.g. `memory:GLMemory`). Empty string asserts the selector is on system memory.

**Possible types**: `string`

**Default**: (null)

---

#### `scale-factory` (_optional_)

Expected scaler factory name.

**Possible types**: `string`

**Default**: (null)

---

#### `strict` (_optional_)

Expected value of ges_video_element_selector_is_strict().

**Possible types**: `boolean`

**Default**: (null)

---

#### `uploader-factory` (_optional_)

Expected uploader factory name. Empty string asserts none.

**Possible types**: `string`

**Default**: (null)

---

#### `videoflip-factory` (_optional_)

Expected videoflip factory name.

**Possible types**: `string`

**Default**: (null)

---

#### `playback-time` (_optional_)

The playback time at which the action will be executed

**Possible variables**:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

**Possible types**: `double,string`

**Default**: 0.0

---

#### `on-message` (_optional_)

Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

**Possible types**: `string`

**Default**: (null)

---
