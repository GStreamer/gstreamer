---
title: Adding Properties
...

# Adding Properties

The primary and most important way of controlling how an element
behaves, is through GObject properties. GObject properties are defined
in the `_class_init ()` function. The element optionally implements a
`_get_property ()` and a `_set_property ()` function. These functions
will be notified if an application changes or requests the value of a
property, and can then fill in the value or take action required for
that property to change value internally.

You probably also want to keep an instance variable around with the
currently configured value of the property that you use in the get and
set functions. Note that `GObject` will not automatically set your
instance variable to the default value, you will have to do that in the
`_init ()` function of your element.

``` c

/* properties */
enum {
  PROP_0,
  PROP_SILENT
  /* FILL ME */
};

static void gst_my_filter_set_property  (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec);
static void gst_my_filter_get_property  (GObject      *object,
                         guint         prop_id,
                         GValue       *value,
                         GParamSpec   *pspec);

static void
gst_my_filter_class_init (GstMyFilterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  /* define virtual function pointers */
  object_class->set_property = gst_my_filter_set_property;
  object_class->get_property = gst_my_filter_get_property;

  /* define properties */
  g_object_class_install_property (object_class, PROP_SILENT,
    g_param_spec_boolean ("silent", "Silent",
              "Whether to be very verbose or not",
              FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_my_filter_set_property (GObject      *object,
                guint         prop_id,
                const GValue *value,
                GParamSpec   *pspec)
{
  GstMyFilter *filter = GST_MY_FILTER (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      g_print ("Silent argument was changed to %s\n",
           filter->silent ? "true" : "false");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_my_filter_get_property (GObject    *object,
                guint       prop_id,
                GValue     *value,
                GParamSpec *pspec)
{
  GstMyFilter *filter = GST_MY_FILTER (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
```

The above is a very simple example of how properties are used. Graphical
applications will use these properties and will display a
user-controllable widget with which these properties can be changed.
This means that - for the property to be as user-friendly as possible -
you should be as exact as possible in the definition of the property.
Not only in defining ranges in between which valid properties can be
located (for integers, floats, etc.), but also in using very descriptive
(better yet: internationalized) strings in the definition of the
property, and if possible using enums and flags instead of integers. The
GObject documentation describes these in a very complete way, but below,
we'll give a short example of where this is useful. Note that using
integers here would probably completely confuse the user, because they
make no sense in this context. The example is stolen from videotestsrc.

``` c
typedef enum {
  GST_VIDEOTESTSRC_SMPTE,
  GST_VIDEOTESTSRC_SNOW,
  GST_VIDEOTESTSRC_BLACK
} GstVideotestsrcPattern;

[..]

#define GST_TYPE_VIDEOTESTSRC_PATTERN (gst_videotestsrc_pattern_get_type ())
static GType
gst_videotestsrc_pattern_get_type (void)
{
  static GType videotestsrc_pattern_type = 0;

  if (!videotestsrc_pattern_type) {
    static GEnumValue pattern_types[] = {
      { GST_VIDEOTESTSRC_SMPTE, "SMPTE 100% color bars",    "smpte" },
      { GST_VIDEOTESTSRC_SNOW,  "Random (television snow)", "snow"  },
      { GST_VIDEOTESTSRC_BLACK, "0% Black",                 "black" },
      { 0, NULL, NULL },
    };

    videotestsrc_pattern_type =
    g_enum_register_static ("GstVideotestsrcPattern",
                pattern_types);
  }

  return videotestsrc_pattern_type;
}

[..]

static void
gst_videotestsrc_class_init (GstvideotestsrcClass *klass)
{
[..]
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PATTERN,
    g_param_spec_enum ("pattern", "Pattern",
               "Type of test pattern to generate",
                       GST_TYPE_VIDEOTESTSRC_PATTERN, GST_VIDEOTESTSRC_SMPTE,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
[..]
}

```
