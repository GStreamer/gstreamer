/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstparse.h: get a pipeline from a text pipeline description
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

#ifndef __GST_PARSE_H__
#define __GST_PARSE_H__

#include <gst/gstelement.h>

G_BEGIN_DECLS

GST_API
GQuark gst_parse_error_quark (void);
/**
 * GST_PARSE_ERROR:
 *
 * Get access to the error quark of the parse subsystem.
 */
#define GST_PARSE_ERROR gst_parse_error_quark ()

/* FIXME 2.0: rename to GstParseLaunchError, this is not only related to
 *parsing */
/**
 * GstParseError:
 * @GST_PARSE_ERROR_SYNTAX: A syntax error occurred.
 * @GST_PARSE_ERROR_NO_SUCH_ELEMENT: The description contained an unknown element
 * @GST_PARSE_ERROR_NO_SUCH_PROPERTY: An element did not have a specified property
 * @GST_PARSE_ERROR_LINK: There was an error linking two pads.
 * @GST_PARSE_ERROR_COULD_NOT_SET_PROPERTY: There was an error setting a property
 * @GST_PARSE_ERROR_EMPTY_BIN: An empty bin was specified.
 * @GST_PARSE_ERROR_EMPTY: An empty description was specified
 * @GST_PARSE_ERROR_DELAYED_LINK: A delayed link did not get resolved.
 * @GST_PARSE_ERROR_SENSITIVE_PROPERTY: A property marked
 *     %GST_PARAM_UNTRUSTED_SENSITIVE was set, or a property aimed at a child
 *     could not be checked because the child never appeared while parsing,
 *     while %GST_PARSE_FLAG_NO_UNTRUSTED was in effect.
 *     (Since: 1.30)
 * @GST_PARSE_ERROR_UNTRUSTED_ELEMENT: An element that is not marked
 *     untrusted-aware was used while %GST_PARSE_FLAG_NO_UNTRUSTED was in
 *     effect, see gst_element_class_mark_as_untrusted_aware().
 *     (Since: 1.30)
 *
 * The different parsing errors that can occur.
 */
/**
 * GST_PARSE_ERROR_SENSITIVE_PROPERTY:
 *
 * A property marked %GST_PARAM_UNTRUSTED_SENSITIVE was set, or a property was
 * aimed at a child that still did not exist when parsing finished, while
 * %GST_PARSE_FLAG_NO_UNTRUSTED was in effect.
 *
 * Since: 1.30
 */
/**
 * GST_PARSE_ERROR_UNTRUSTED_ELEMENT:
 *
 * The description used an element that is not marked untrusted-aware, see
 * gst_element_class_mark_as_untrusted_aware(), while
 * %GST_PARSE_FLAG_NO_UNTRUSTED was in effect.
 *
 * Since: 1.30
 */
typedef enum
{
  GST_PARSE_ERROR_SYNTAX,
  GST_PARSE_ERROR_NO_SUCH_ELEMENT,
  GST_PARSE_ERROR_NO_SUCH_PROPERTY,
  GST_PARSE_ERROR_LINK,
  GST_PARSE_ERROR_COULD_NOT_SET_PROPERTY,
  GST_PARSE_ERROR_EMPTY_BIN,
  GST_PARSE_ERROR_EMPTY,
  GST_PARSE_ERROR_DELAYED_LINK,
  GST_PARSE_ERROR_SENSITIVE_PROPERTY,
  GST_PARSE_ERROR_UNTRUSTED_ELEMENT
} GstParseError;

/**
 * GstParseFlags:
 * @GST_PARSE_FLAG_NONE: Do not use any special parsing options.
 * @GST_PARSE_FLAG_FATAL_ERRORS: Always return %NULL when an error occurs
 *     (default behaviour is to return partially constructed bins or elements
 *      in some cases)
 * @GST_PARSE_FLAG_NO_SINGLE_ELEMENT_BINS: If a bin only has a single element,
 *     just return the element.
 * @GST_PARSE_FLAG_PLACE_IN_BIN: If more than one toplevel element is described
 *     by the pipeline description string, put them in a #GstBin instead of a
 *     #GstPipeline. (Since: 1.10)
 * @GST_PARSE_FLAG_NO_UNTRUSTED: Refuse to set any
 *     property marked %GST_PARAM_UNTRUSTED_SENSITIVE. Parsing fails with a
 *     %GST_PARSE_ERROR_SENSITIVE_PROPERTY error before the
 *     property is ever set on the element, so properties that perform I/O from
 *     their setter cannot cause side effects. A property aimed at a child that
 *     does not exist yet is checked when the child appears while parsing
 *     (request pads); a child that never appears makes the parse fail, as the
 *     property cannot be checked. Use this when building a pipeline
 *     from an untrusted description. The blocked assignments are recorded in the
 *     #GstParseContext, see
 *     gst_parse_context_get_untrusted_report().
 *     (Since: 1.30)
 *
 * Parsing options.
 */
/**
 * GST_PARSE_FLAG_NO_UNTRUSTED:
 *
 * Refuse to set any property marked %GST_PARAM_UNTRUSTED_SENSITIVE, failing the
 * parse with %GST_PARSE_ERROR_SENSITIVE_PROPERTY before the property is set, so
 * a setter that performs I/O never runs. A property aimed at a child that does
 * not exist yet is checked at the moment the child appears while parsing, as
 * happens for request pads created by links in the description. A child that
 * still has not appeared when parsing finishes (a pad only added once the
 * pipeline is running, for example a demuxer pad) can never be checked, so the
 * parse is refused. Use this when building a pipeline from an untrusted
 * description; what was refused is recorded in the #GstParseContext, see
 * gst_parse_context_get_untrusted_report().
 *
 * The refused setter never runs, but as with any parse error the partially
 * built pipeline is still returned unless %GST_PARSE_FLAG_FATAL_ERRORS is also
 * set, so check the error, not only the return value.
 *
 * Since: 1.30
 */
typedef enum
{
  GST_PARSE_FLAG_NONE = 0,
  GST_PARSE_FLAG_FATAL_ERRORS = (1 << 0),
  GST_PARSE_FLAG_NO_SINGLE_ELEMENT_BINS = (1 << 1),
  GST_PARSE_FLAG_PLACE_IN_BIN = (1 << 2),
  GST_PARSE_FLAG_NO_UNTRUSTED = (1 << 3)
} GstParseFlags;

#define GST_TYPE_PARSE_CONTEXT (gst_parse_context_get_type())

/**
 * GstParseContext:
 *
 * Opaque structure.
 */
typedef struct _GstParseContext GstParseContext;

/* create, process and free a parse context */

GST_API
GType             gst_parse_context_get_type (void);

GST_API
GstParseContext * gst_parse_context_new (void) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;

GST_API
gchar          ** gst_parse_context_get_missing_elements (GstParseContext * context) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;

GST_API
GstStructure    * gst_parse_context_get_untrusted_report (GstParseContext * context) G_GNUC_WARN_UNUSED_RESULT;

GST_API
gboolean          gst_parse_untrusted_report (const GstStructure * report, gchar *** elements,
                                              GstStructure ** properties,
                                              GstStructure ** unresolved_properties);

GST_API
void              gst_parse_context_free (GstParseContext * context);

GST_API
GstParseContext * gst_parse_context_copy (const GstParseContext * context) G_GNUC_WARN_UNUSED_RESULT;


/* parse functions */

GST_API
GstElement      * gst_parse_launch       (const gchar      * pipeline_description,
                                          GError          ** error) G_GNUC_MALLOC;
GST_API
GstElement      * gst_parse_launchv      (const gchar     ** argv,
                                          GError          ** error) G_GNUC_MALLOC;
GST_API
GstElement      * gst_parse_launch_full  (const gchar      * pipeline_description,
                                          GstParseContext  * context,
                                          GstParseFlags      flags,
                                          GError          ** error) G_GNUC_MALLOC;
GST_API
GstElement      * gst_parse_launchv_full (const gchar     ** argv,
                                          GstParseContext  * context,
                                          GstParseFlags      flags,
                                          GError          ** error) G_GNUC_MALLOC;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstParseContext, gst_parse_context_free)

G_END_DECLS

#endif /* __GST_PARSE_H__ */
