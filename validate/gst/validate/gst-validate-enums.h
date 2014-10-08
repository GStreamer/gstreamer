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
 * GstValidateReportingLevel:
 * @GST_VALIDATE_REPORTING_LEVEL_NONE: No debugging level specified or desired. Used to deactivate
 *  debugging output.
 * @GST_VALIDATE_REPORTING_LEVEL_SYNTHETIC: Summary of the issues found, with no
 *  details.
 * @GST_VALIDATE_REPORTING_LEVEL_SUBCHAIN: If set as the default level, similar
 *  issues can be reported multiple times for different subchains.
 *  If set as the level for a particular object (my_object:subchain), validate
 *  will report the issues where the object is the first to report an issue for
 *  a subchain.
 * @GST_VALIDATE_REPORTING_LEVEL_MONITOR: If set as the default level, all the
 * distinct issues for all the monitors will be reported.
 * If set as the level for a particular object, all the distinct issues for this object
 * will be reported.
 * Note that if the same issue happens twice on the same object, up until this
 * level that issue is only reported once.
 * @GST_VALIDATE_REPORTING_LEVEL_ALL: All the issues will be reported, even those
 * that repeat themselves inside the same object. This can be *very* verbose if
 * set globally.
 * @GST_VALIDATE_REPORTING_LEVEL_UNKNOWN: No reporting level known,
 * reporting will default to the global reporting level.
 *
 * Setting the reporting level allows to control the way issues are reported
 * when calling #gst_validate_runner_printf.
 *
 * The reporting level can be set through the "GST_VALIDATE_REPORTING_LEVEL"
 * environment variable, as  a comma-separated list of (optional) object categories / names
 * and levels. No object category / name sets the global level.
 *
 * Examples: GST_VALIDATE_REPORTING_LEVEL=synthetic,h264parse:all
 *           GST_VALIDATE_REPORTING_LEVEL=none,h264parse::sink_0:synthetic
 */
typedef enum {
  GST_VALIDATE_REPORTING_LEVEL_UNKNOWN = 0,
  GST_VALIDATE_REPORTING_LEVEL_NONE = 1,
  GST_VALIDATE_REPORTING_LEVEL_SYNTHETIC = 2,
  GST_VALIDATE_REPORTING_LEVEL_SUBCHAIN = 3,
  GST_VALIDATE_REPORTING_LEVEL_MONITOR = 4,
  GST_VALIDATE_REPORTING_LEVEL_ALL = 5,
  GST_VALIDATE_REPORTING_LEVEL_COUNT
} GstValidateReportingLevel;

/**
 * GST_VALIDATE_REPORTING_LEVEL_DEFAULT:
 *
 * Defines the default reporting level to be used with gst-validate. It is normally
 * set to #GST_VALIDATE_REPORTING_LEVEL_SYNTHETIC so only a synthetic report
 * gets printed.
 * As it can be configured at compile time, developer builds may chose to
 * override that though.
 */
#ifndef GST_VALIDATE_REPORTING_LEVEL_DEFAULT
#define GST_VALIDATE_REPORTING_LEVEL_DEFAULT GST_VALIDATE_REPORTING_LEVEL_SYNTHETIC
#endif

#endif /* __GST_VALIDATE_RUNNER_H__ */
