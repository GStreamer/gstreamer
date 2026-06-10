/* GStreamer - GParamSpecs for some of our types
 * Copyright (C) 2007 Tim-Philipp Müller  <tim centricular net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_PARAMSPECS_H__
#define __GST_PARAMSPECS_H__

#include <gst/gstvalue.h>

G_BEGIN_DECLS

/* --- paramspec flags */

/**
 * GST_PARAM_CONTROLLABLE: (value 512)
 *
 * Use this flag on GObject properties to signal they can make sense to be.
 * controlled over time. This hint is used by the GstController.
 */
#define	GST_PARAM_CONTROLLABLE	(1 << (G_PARAM_USER_SHIFT + 1))

/**
 * GST_PARAM_MUTABLE_READY: (value 1024)
 *
 * Use this flag on GObject properties of GstElements to indicate that
 * they can be changed when the element is in the READY or lower state.
 */
#define GST_PARAM_MUTABLE_READY  (1 << (G_PARAM_USER_SHIFT + 2))

/**
 * GST_PARAM_MUTABLE_PAUSED: (value 2048)
 *
 * Use this flag on GObject properties of GstElements to indicate that
 * they can be changed when the element is in the PAUSED or lower state.
 * This flag implies GST_PARAM_MUTABLE_READY.
 */
#define GST_PARAM_MUTABLE_PAUSED  (1 << (G_PARAM_USER_SHIFT + 3))

/**
 * GST_PARAM_MUTABLE_PLAYING: (value 4096)
 *
 * Use this flag on GObject properties of GstElements to indicate that
 * they can be changed when the element is in the PLAYING or lower state.
 * This flag implies GST_PARAM_MUTABLE_PAUSED.
 */
#define GST_PARAM_MUTABLE_PLAYING  (1 << (G_PARAM_USER_SHIFT + 4))

/**
 * GST_PARAM_DOC_SHOW_DEFAULT: (value 8192)
 *
 * Use this flag on GObject properties of GstObject to indicate that
 * during `gst-inspect` and friends, the default value should be used
 * as default instead of the current value.
 *
 * Since: 1.18
 */
#define GST_PARAM_DOC_SHOW_DEFAULT  (1 << (G_PARAM_USER_SHIFT + 5))

/**
 * GST_PARAM_CONDITIONALLY_AVAILABLE: (value 16384)
 *
 * Use this flag on GObject properties of GstObject to indicate that
 * they might not be available depending on environment such as OS, device, etc,
 * so such properties will be installed conditionally only if the GstObject is
 * able to support it.
 *
 * Since: 1.18
 */
#define GST_PARAM_CONDITIONALLY_AVAILABLE  (1 << (G_PARAM_USER_SHIFT + 6))

/**
 * GST_PARAM_UNTRUSTED_SENSITIVE: (value 32768)
 *
 * Use this flag on GObject properties whose value can make the element reach
 * outside its media processing: read or write a file, open a network
 * connection, select a device or IPC endpoint, load a plugin or library, or
 * run externally supplied code (shader or script source). Setting such a
 * property from data that comes from an untrusted source (for example a
 * serialized pipeline or project file that was downloaded or received from a
 * third party) is a security risk.
 *
 * Consumers that build pipelines from untrusted descriptions can ask
 * gst_parse_launch_full() to refuse these properties with
 * %GST_PARSE_FLAG_NO_UNTRUSTED.
 *
 * This flag only takes effect on elements whose class is also marked with
 * gst_element_class_mark_as_untrusted_aware(): an unmarked element is never
 * instantiated from an untrusted description in the first place, so an element
 * that sets this flag on a property must also mark its class.
 *
 * Since: 1.30
 */
#define GST_PARAM_UNTRUSTED_SENSITIVE  (1 << (G_PARAM_USER_SHIFT + 7))

/**
 * GST_PARAM_USER_SHIFT: (value 65536)
 *
 * Bits based on GST_PARAM_USER_SHIFT can be used by 3rd party applications.
 */
#define	GST_PARAM_USER_SHIFT	(1 << (G_PARAM_USER_SHIFT + 8))


/**
 * GST_PROP_KIND_LOCATION:
 *
 * Kind name for properties naming a resource to be opened, read or
 * written. The %GST_PROP_KIND_FIELD_SUBTYPE field says how the resource is
 * addressed.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_LOCATION "location"

/**
 * GST_PROP_KIND_NETWORK:
 *
 * Kind name for properties naming a network endpoint.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_NETWORK "network"

/**
 * GST_PROP_KIND_SECURITY:
 *
 * Kind name for properties holding a credential.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_SECURITY "security"

/**
 * GST_PROP_KIND_COLOR:
 *
 * Kind name for properties describing a color.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_COLOR "color"

/**
 * GST_PROP_KIND_BITRATE:
 *
 * Kind name for properties describing a bitrate. The %GST_PROP_KIND_FIELD_UNIT
 * field gives the unit of the value; bits per second when absent.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_BITRATE "bitrate"

/**
 * GST_PROP_KIND_FONT:
 *
 * Kind name for properties describing a font.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_FONT "font"

/**
 * GST_PROP_KIND_FIELD_SUBTYPE:
 *
 * Kind field (string) refining how the value is encoded, e.g.
 * %GST_PROP_KIND_SUBTYPE_PATH.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_FIELD_SUBTYPE "subtype"

/**
 * GST_PROP_KIND_FIELD_UNIT:
 *
 * Kind field (string) giving the unit of a numeric value, e.g.
 * %GST_PROP_KIND_UNIT_KILOBITS_PER_SEC.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_FIELD_UNIT "unit"

/**
 * GST_PROP_KIND_FIELD_MIMETYPES:
 *
 * Kind field listing the media types the resource is expected to hold,
 * e.g. `image/png`. Either a string or a #GstValueList of strings, so
 * readers must check the value type: gst_structure_get_string() returns
 * %NULL for a list.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_FIELD_MIMETYPES "mimetypes"

/**
 * GST_PROP_KIND_FIELD_EXTENSIONS:
 *
 * Kind field listing the file extensions the resource is expected to have,
 * without the leading dot, e.g. `onnx`. Either a string or a #GstValueList
 * of strings, so readers must check the value type:
 * gst_structure_get_string() returns %NULL for a list.
 * Meant as a filter hint for file choosers, not as a restriction enforced by
 * the element.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_FIELD_EXTENSIONS "extensions"

/**
 * GST_PROP_KIND_SUBTYPE_PATH:
 *
 * A resource addressed by a filesystem path, absolute or relative. Use as
 * %GST_PROP_KIND_FIELD_SUBTYPE of %GST_PROP_KIND_LOCATION.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_SUBTYPE_PATH "path"

/**
 * GST_PROP_KIND_SUBTYPE_URI:
 *
 * A resource addressed by a URI, e.g. `scheme://...`. Whether the URI is
 * local or remote depends on the value set at runtime, so it is not part of
 * the hint. Use as %GST_PROP_KIND_FIELD_SUBTYPE of
 * %GST_PROP_KIND_LOCATION.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_SUBTYPE_URI "uri"

/**
 * GST_PROP_KIND_SUBTYPE_PATTERN:
 *
 * A printf-style pattern expanded into filesystem paths, as used by
 * `multifilesink` and `splitmuxsink` (e.g. `video%05d.mp4`). Distinct from
 * %GST_PROP_KIND_SUBTYPE_PATH because picking a single existing file yields a
 * pattern that matches only itself. Use as
 * %GST_PROP_KIND_FIELD_SUBTYPE of %GST_PROP_KIND_LOCATION.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_SUBTYPE_PATTERN "pattern"

/**
 * GST_PROP_KIND_SUBTYPE_DIRECTORY:
 *
 * A filesystem path naming a directory rather than a file. Use as
 * %GST_PROP_KIND_FIELD_SUBTYPE of %GST_PROP_KIND_LOCATION.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_SUBTYPE_DIRECTORY "directory"

/**
 * GST_PROP_KIND_SUBTYPE_HOSTNAME:
 *
 * A host name, IP address or network interface. Use as
 * %GST_PROP_KIND_FIELD_SUBTYPE of %GST_PROP_KIND_NETWORK.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_SUBTYPE_HOSTNAME "hostname"

/**
 * GST_PROP_KIND_SUBTYPE_PASSWORD:
 *
 * A password, token or other credential. Editors should mask it, and tooling
 * should keep it out of logs and pipeline dumps. Use as
 * %GST_PROP_KIND_FIELD_SUBTYPE of %GST_PROP_KIND_SECURITY.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_SUBTYPE_PASSWORD "password"

/**
 * GST_PROP_KIND_SUBTYPE_ARGB:
 *
 * A color packed as `0xAARRGGBB` in an integer property. Use as
 * %GST_PROP_KIND_FIELD_SUBTYPE of %GST_PROP_KIND_COLOR.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_SUBTYPE_ARGB "argb"

/**
 * GST_PROP_KIND_UNIT_BITS_PER_SEC:
 *
 * Bits per second. Use as %GST_PROP_KIND_FIELD_UNIT of
 * %GST_PROP_KIND_BITRATE.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_UNIT_BITS_PER_SEC "bits-per-sec"

/**
 * GST_PROP_KIND_UNIT_KILOBITS_PER_SEC:
 *
 * Kilobits per second (1000 bits per second). Use as
 * %GST_PROP_KIND_FIELD_UNIT of %GST_PROP_KIND_BITRATE.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_UNIT_KILOBITS_PER_SEC "kilobits-per-sec"

/**
 * GST_PROP_KIND_UNIT_BYTES_PER_SEC:
 *
 * Bytes per second. Use as %GST_PROP_KIND_FIELD_UNIT of
 * %GST_PROP_KIND_BITRATE.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_UNIT_BYTES_PER_SEC "bytes-per-sec"

/**
 * GST_PROP_KIND_SUBTYPE_DESC:
 *
 * A Pango font description, e.g. `Sans Bold 12`. Use as
 * %GST_PROP_KIND_FIELD_SUBTYPE of %GST_PROP_KIND_FONT.
 *
 * Since: 1.30
 */
#define GST_PROP_KIND_SUBTYPE_DESC "desc"

/* --- type macros --- */

/**
 * GstParamFraction:
 *
 * A fundamental type that describes a #GParamSpec for fractional
 * properties
 */

#define GST_TYPE_PARAM_FRACTION           (gst_param_spec_fraction_get_type ())
#define GST_IS_PARAM_SPEC_FRACTION(pspec) (G_TYPE_CHECK_INSTANCE_TYPE ((pspec), GST_TYPE_PARAM_FRACTION))
#define GST_PARAM_SPEC_FRACTION(pspec)    (G_TYPE_CHECK_INSTANCE_CAST ((pspec), GST_TYPE_PARAM_FRACTION, GstParamSpecFraction))

/**
 * GstParamArray:
 *
 * A fundamental type that describes a #GParamSpec for arrays of
 * values
 *
 * Since: 1.12
 */

#define GST_TYPE_PARAM_ARRAY_LIST           (gst_param_spec_array_get_type ())
#define GST_IS_PARAM_SPEC_ARRAY_LIST(pspec) (G_TYPE_CHECK_INSTANCE_TYPE ((pspec), GST_TYPE_PARAM_ARRAY_LIST))
#define GST_PARAM_SPEC_ARRAY_LIST(pspec)    (G_TYPE_CHECK_INSTANCE_CAST ((pspec), GST_TYPE_PARAM_ARRAY_LIST, GstParamSpecArray))


/* --- get_type functions --- */

GST_API
GType  gst_param_spec_fraction_get_type (void);

GST_API
GType  gst_param_spec_array_get_type (void);


/* --- typedefs & structures --- */

typedef struct _GstParamSpecFraction GstParamSpecFraction;
typedef struct _GstParamSpecArray GstParamSpecArray;

/**
 * GstParamSpecFraction:
 * @parent_instance: super class
 * @min_num: minimal numerator
 * @min_den: minimal denominator
 * @max_num: maximal numerator
 * @max_den: maximal denominator
 * @def_num: default numerator
 * @def_den: default denominator
 *
 * A GParamSpec derived structure that contains the meta data for fractional
 * properties.
 */
struct _GstParamSpecFraction {
  GParamSpec    parent_instance;

  gint          min_num, min_den;
  gint          max_num, max_den;
  gint          def_num, def_den;
};

/**
 * GstParamSpecArray:
 * @parent_instance: super class
 * @element_spec: the #GParamSpec of the type of values in the array
 *
 * A GParamSpec derived structure for arrays of values.
 */
struct _GstParamSpecArray {
  GParamSpec    parent_instance;

  GParamSpec * element_spec;
};


/* --- GParamSpec prototypes --- */

GST_API
GParamSpec  * gst_param_spec_fraction (const gchar * name,
                                       const gchar * nick,
                                       const gchar * blurb,
                                       gint min_num, gint min_denom,
                                       gint max_num, gint max_denom,
                                       gint default_num, gint default_denom,
                                       GParamFlags flags) G_GNUC_MALLOC;
GST_API
GParamSpec  * gst_param_spec_array    (const gchar * name,
                                       const gchar * nick,
                                       const gchar * blurb,
                                       GParamSpec * element_spec,
                                       GParamFlags flags) G_GNUC_MALLOC;

GST_API
void          gst_param_spec_set_kind (GParamSpec * pspec,
                                       GstStructure * kind);

GST_API
const GstStructure * gst_param_spec_get_kind (GParamSpec * pspec);

G_END_DECLS

#endif /* __GST_PARAMSPECS_H__ */

