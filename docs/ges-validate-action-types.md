# GES action types

## edit-container

``` validate-scenario
edit-container,
    [playback-time=(double,string)],
    container-name=(string),
    position=(double or string),
    [edit-mode=(string)],
    [edge=(string)],
    [new-layer-priority=(int)];
```

Allows to edit a container (like a GESClip), for more details, have a look at:
ges_container_edit documentation, Note that the timeline will
be commited, and flushed so that the edition is taken into account
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `container-name`:(mandatory): The name of the GESContainer to edit

  Possible types: `string`

* `position`:(mandatory): The new position of the GESContainer

  Possible variables:

  * position: The current position in the stream

  * duration: The duration of the stream

  Possible types: `double or string`

* `edit-mode`:(optional): The GESEditMode to use to edit @container-name

  Possible types: `string`

  Default: normal

* `edge`:(optional): The GESEdge to use to edit @container-name
should be in [ edge_start, edge_end, edge_none ]

  Possible types: `string`

  Default: edge_none

* `new-layer-priority`:(optional): The priority of the layer @container should land in.
If the layer you're trying to move the container to doesn't exist, it will
be created automatically. -1 means no move.

  Possible types: `int`

  Default: -1

## add-asset


``` validate-scenario
add-asset,
    [playback-time=(double,string)],
    id,
    type;
```

Allows to add an asset to the current project
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `id`:(mandatory): Adds an asset to a project.

* `type`:(mandatory): The type of asset to add

## remove-asset


``` validate-scenario
remove-asset,
    [playback-time=(double,string)],
    id,
    type;
```

Allows to remove an asset from the current project
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `id`:(mandatory): The ID of the clip to remove

* `type`:(mandatory): The type of asset to remove

## add-layer


``` validate-scenario
add-layer,
    [playback-time=(double,string)],
    [priority];
```

Allows to add a layer to the current timeline
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `priority`:(optional): The priority of the new layer to add,if not specified, the new layer will be appended to the timeline

  Default: (null)

## remove-layer


``` validate-scenario
remove-layer,
    [playback-time=(double,string)],
    priority,
    [auto-transition=(boolean)];
```

Allows to remove a layer from the current timeline
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `priority`:(mandatory): The priority of the layer to remove

* `auto-transition`:(optional): Wheter auto-transition is activated on the new layer.

  Possible types: `boolean`

  Default: False

## add-clip


``` validate-scenario
add-clip,
    [playback-time=(double,string)],
    name=(string),
    layer-priority=(int),
    asset-id=(string),
    type=(string),
    [start=(double or string)],
    [inpoint=(double or string)],
    [duration=(double or string)];
```

Allows to add a clip to a given layer
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `name`:(mandatory): The name of the clip to add

  Possible types: `string`

* `layer-priority`:(mandatory): The priority of the clip to add

  Possible types: `int`

* `asset-id`:(mandatory): The id of the asset from which to extract the clip

  Possible types: `string`

* `type`:(mandatory): The type of the clip to create

  Possible types: `string`

* `start`:(optional): The start value to set on the new GESClip.

  Possible types: `double or string`

  Default: (null)

* `inpoint`:(optional): The  inpoint value to set on the new GESClip

  Possible types: `double or string`

  Default: (null)

* `duration`:(optional): The  duration value to set on the new GESClip

  Possible types: `double or string`

  Default: (null)

## remove-clip


``` validate-scenario
remove-clip,
    [playback-time=(double,string)],
    name=(string);
```

Allows to remove a clip from a given layer
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `name`:(mandatory): The name of the clip to remove

  Possible types: `string`

## serialize-project


``` validate-scenario
serialize-project,
    [playback-time=(double,string)],
    uri=(string);
```

serializes a project
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `uri`:(mandatory): The uri where to store the serialized project

  Possible types: `string`

## set-child-property


``` validate-scenario
set-child-property,
    [playback-time=(double,string)],
    element-name=(string),
    property=(string),
    value=(gvalue);
```

Allows to change child property of an object
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `element-name`:(mandatory): The name of the element on which to modify the property

  Possible types: `string`

* `property`:(mandatory): The name of the property to modify

  Possible types: `string`

* `value`:(mandatory): The value of the property

  Possible types: `gvalue`

## split-clip


``` validate-scenario
split-clip,
    [playback-time=(double,string)],
    clip-name=(string),
    position=(double or string);
```

Split a clip at a specified position.
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `clip-name`:(mandatory): The name of the clip to split

  Possible types: `string`

* `position`:(mandatory): The position at which to split the clip

  Possible types: `double or string`

## set-track-restriction-caps


``` validate-scenario
set-track-restriction-caps,
    [playback-time=(double,string)],
    track-type=(string),
    caps=(string);
```

Sets restriction caps on tracks of a specific type.
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `track-type`:(mandatory): The type of track to set restriction caps on

  Possible types: `string`

* `caps`:(mandatory): The caps to set on the track

  Possible types: `string`

## element-set-asset


``` validate-scenario
element-set-asset,
    [playback-time=(double,string)],
    element-name=(string),
    asset-id=(string);
```

Sets restriction caps on tracks of a specific type.
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `element-name`:(mandatory): The name of the TimelineElement to set an asset on

  Possible types: `string`

* `asset-id`:(mandatory): The id of the asset from which to extract the clip

  Possible types: `string`

## container-add-child


``` validate-scenario
container-add-child,
    [playback-time=(double,string)],
    container-name=(string),
    [child-name=(string)],
    asset-id=(string),
    [child-type=(string)];
```

Add a child to @container-name. If asset-id and child-type are specified, the child will be created and added. Otherwize @child-name has to be specified and will be added to the container.
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `container-name`:(mandatory): The name of the GESContainer to add a child to

  Possible types: `string`

* `child-name`:(optional): The name of the child to add to @container-name

  Possible types: `string`

  Default: NULL

* `asset-id`:(mandatory): The id of the asset from which to extract the child

  Possible types: `string`

* `child-type`:(optional): The type of the child to create

  Possible types: `string`

  Default: NULL

## container-remove-child


``` validate-scenario
container-remove-child,
    [playback-time=(double,string)],
    container-name=(string),
    child-name=(string);
```

Remove a child from @container-name.
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `container-name`:(mandatory): The name of the GESContainer to remove a child from

  Possible types: `string`

* `child-name`:(mandatory): The name of the child to reomve from @container-name

  Possible types: `string`

## ungroup-container


``` validate-scenario
ungroup-container,
    [playback-time=(double,string)],
    container-name=(string),
    [recursive=(boolean)];
```

Ungroup children of @container-name.
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `container-name`:(mandatory): The name of the GESContainer to ungroup children from

  Possible types: `string`

* `recursive`:(optional): Wether to recurse ungrouping or not.

  Possible types: `boolean`

  Default: (null)

## set-control-source


``` validate-scenario
set-control-source,
    [playback-time=(double,string)],
    element-name=(string),
    property-name=(string),
    [binding-type=(string)],
    [source-type=(string)],
    [interpolation-mode=(string)];
```

Adds a GstControlSource on @element-name::@property-name allowing you to then add keyframes on that property.
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `element-name`:(mandatory): The name of the GESTrackElement to set the control source on

  Possible types: `string`

* `property-name`:(mandatory): The name of the property for which to set a control source

  Possible types: `string`

* `binding-type`:(optional): The name of the type of binding to use

  Possible types: `string`

  Default: direct

* `source-type`:(optional): The name of the type of ControlSource to use

  Possible types: `string`

  Default: interpolation

* `interpolation-mode`:(optional): The name of the GstInterpolationMode to on the source

  Possible types: `string`

  Default: linear

## add-keyframe


``` validate-scenario
add-keyframe,
    [playback-time=(double,string)],
    element-name=(string),
    property-name=(string),
    timestamp=(string or float),
    value=(float);
```

Remove a child from @container-name.
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `element-name`:(mandatory): The name of the GESTrackElement to add a keyframe on

  Possible types: `string`

* `property-name`:(mandatory): The name of the property for which to add a keyframe on

  Possible types: `string`

* `timestamp`:(mandatory): The timestamp of the keyframe

  Possible types: `string or float`

* `value`:(mandatory): The value of the keyframe

  Possible types: `float`

## copy-element


``` validate-scenario
copy-element,
    [playback-time=(double,string)],
    element-name=(string),
    [recurse=(boolean)],
    position=(string or float),
    [paste-name=(string)];
```

Remove a child from @container-name.
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `element-name`:(mandatory): The name of the GESTtimelineElement to copy

  Possible types: `string`

* `recurse`:(optional): Copy recursively or not

  Possible types: `boolean`

  Default: true

* `position`:(mandatory): The time where to paste the element

  Possible types: `string or float`

* `paste-name`:(optional): The name of the copied element

  Possible types: `string`

  Default: (null)

## remove-keyframe


``` validate-scenario
remove-keyframe,
    [playback-time=(double,string)],
    element-name=(string),
    property-name=(string),
    timestamp=(string or float);
```

Remove a child from @container-name.
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `element-name`:(mandatory): The name of the GESTrackElement to add a keyframe on

  Possible types: `string`

* `property-name`:(mandatory): The name of the property for which to add a keyframe on

  Possible types: `string`

* `timestamp`:(mandatory): The timestamp of the keyframe

  Possible types: `string or float`

## load-project


``` validate-scenario
load-project,
    [playback-time=(double,string)],
    serialized-content;
```

Loads a project either from its content passed in the serialized-content field.
Note that it will completely clean the previous timeline
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

* `serialized-content`:(mandatory): The full content of the XML describing project in XGES formet.

## commit


``` validate-scenario
commit,
    [playback-time=(double,string)];
```

Commit the timeline.
 * Implementer namespace: ges

### Parameters

  Parameters:

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)
