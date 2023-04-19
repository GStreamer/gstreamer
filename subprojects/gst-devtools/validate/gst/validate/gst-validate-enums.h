/* GStreamer
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@collabora.com>
 *
 * gst-validate-enums.h - Validate constants.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_VALIDATE_ENUMS_H__
#define __GST_VALIDATE_ENUMS_H__

/**
 * SECTION: gst-validate-enums.h
 * @title: GstValidate enums
 */

/**
 * GstValidateReportingDetails:
 * @GST_VALIDATE_SHOW_NONE: No debugging level specified or desired. Used to deactivate
 *  debugging output.
 * @GST_VALIDATE_SHOW_SMART: Sythetic for not fatal issues and detailed for
 * others
 * @GST_VALIDATE_SHOW_SYNTHETIC: Summary of the issues found, with no
 *  details.
 * @GST_VALIDATE_SHOW_SUBCHAIN: If set as the default level, similar
 *  issues can be reported multiple times for different subchains.
 *  If set as the level for a particular object (my_object:subchain), validate
 *  will report the issues where the object is the first to report an issue for
 *  a subchain.
 * @GST_VALIDATE_SHOW_MONITOR: If set as the default level, all the
 * distinct issues for all the monitors will be reported.
 * If set as the level for a particular object, all the distinct issues for this object
 * will be reported.
 * Note that if the same issue happens twice on the same object, up until this
 * level that issue is only reported once.
 * @GST_VALIDATE_SHOW_ALL: All the issues will be reported, even those
 * that repeat themselves inside the same object. This can be *very* verbose if
 * set globally.
 * @GST_VALIDATE_SHOW_UNKNOWN: No reporting level known,
 * reporting will default to the global reporting level.
 *
 * Setting the reporting level allows to control the way issues are reported
 * when calling #gst_validate_runner_printf.
 *
 * The reporting level can be set through the "GST_VALIDATE_REPORTING_DETAILS"
 * environment variable, as  a comma-separated list of (optional) object categories / names
 * and levels. No object category / name sets the global level.
 *
 * Examples: GST_VALIDATE_REPORTING_DETAILS=synthetic,h264parse:all
 *           GST_VALIDATE_REPORTING_DETAILS=none,h264parse::sink_0:synthetic
 */
typedef enum {
  GST_VALIDATE_SHOW_UNKNOWN = 0,
  GST_VALIDATE_SHOW_NONE = 1,
  GST_VALIDATE_SHOW_SYNTHETIC = 2,
  GST_VALIDATE_SHOW_SUBCHAIN = 3,
  GST_VALIDATE_SHOW_MONITOR = 4,
  GST_VALIDATE_SHOW_ALL = 5,
  GST_VALIDATE_SHOW_SMART = 6,
  GST_VALIDATE_SHOW_COUNT
} GstValidateReportingDetails;

/**
 * GST_VALIDATE_SHOW_DEFAULT:
 *
 * Defines the default reporting level to be used with gst-validate. It is normally
 * set to #GST_VALIDATE_SHOW_SYNTHETIC so only a synthetic report
 * gets printed.
 * As it can be configured at compile time, developer builds may chose to
 * override that though.
 */
#ifndef GST_VALIDATE_SHOW_DEFAULT
#define GST_VALIDATE_SHOW_DEFAULT GST_VALIDATE_SHOW_SMART
#endif

/**
 * GstValidateVerbosityFlags:
 *
 * Defines the level of verbosity of -validate (ie, printing on stdout).
 */
typedef enum
{
  GST_VALIDATE_VERBOSITY_NONE = 0,
  GST_VALIDATE_VERBOSITY_POSITION = 1 << 1,
  GST_VALIDATE_VERBOSITY_MESSAGES = 1 << 2,
  GST_VALIDATE_VERBOSITY_PROPS_CHANGES = 1 << 3,
  GST_VALIDATE_VERBOSITY_NEW_ELEMENTS = 1 << 4,
  GST_VALIDATE_VERBOSITY_ALL = GST_VALIDATE_VERBOSITY_POSITION | GST_VALIDATE_VERBOSITY_MESSAGES | GST_VALIDATE_VERBOSITY_PROPS_CHANGES | GST_VALIDATE_VERBOSITY_NEW_ELEMENTS
} GstValidateVerbosityFlags;

/**
 * GstValidateStructureResolveVariablesFlags:
 *
 * Since: 1.20
 */
typedef enum {
    GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_ALL = 0,
    GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_LOCAL_ONLY = 1 << 0,
    GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_NO_FAILURE = 1 << 1,
    GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_NO_EXPRESSION = 1 << 1,
} GstValidateStructureResolveVariablesFlags;


/**
 * GstValidateObjectSetPropertyFlags:
 * @GST_VALIDATE_OBJECT_SET_PROPERTY_FLAGS_OPTIONAL: The property is optional, if it
 * is not found on the object, nothing happens.
 * @GST_VALIDATE_OBJECT_SET_PROPERTY_FLAGS_NO_VALUE_CHECK: Do not check that after
 * setting the property, the value is the one we set.
 *
 * Since: 1.24
 */
typedef enum {
  GST_VALIDATE_OBJECT_SET_PROPERTY_FLAGS_OPTIONAL = (1 << 0),
  GST_VALIDATE_OBJECT_SET_PROPERTY_FLAGS_NO_VALUE_CHECK = (1 << 1),
} GstValidateObjectSetPropertyFlags;

#endif /* __GST_VALIDATE_ENUMS_H__ */
