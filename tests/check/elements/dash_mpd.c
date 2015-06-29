/* GStreamer unit test for MPEG-DASH
 *
 * Copyright (c) <2015> YouView TV Ltd
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

#include "../../ext/dash/gstmpdparser.c"
#undef GST_CAT_DEFAULT

#include <gst/check/gstcheck.h>

GST_DEBUG_CATEGORY (gst_dash_demux_debug);

/*
 * compute the number of milliseconds contained in a duration value specified by
 * year, month, day, hour, minute, second, millisecond
 *
 * This function must use the same conversion algorithm implemented in
 * gst_mpdparser_get_xml_prop_duration from gstmpdparser.c file.
 */
static guint64
duration_to_ms (guint year, guint month, guint day, guint hour, guint minute,
    guint second, guint millisecond)
{
  guint64 days = (guint64) year * 365 + (guint64) month * 30 + day;
  guint64 hours = days * 24 + hour;
  guint64 minutes = hours * 60 + minute;
  guint64 seconds = minutes * 60 + second;
  guint64 ms = seconds * 1000 + millisecond;
  return ms;
}

/*
 * Test to ensure a simple mpd file successfully parses.
 *
 */
GST_START_TEST (dash_mpdparser_validsimplempd)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\"> </MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  /* check that unset elements with default values are properly configured */
  assert_equals_int (mpdclient->mpd_node->type, GST_MPD_FILE_TYPE_STATIC);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the MPD attributes.
 *
 */
GST_START_TEST (dash_mpdparser_mpd)
{
  GstDateTime *availabilityStartTime;
  GstDateTime *availabilityEndTime;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      " schemaLocation=\"TestSchemaLocation\""
      " xmlns:xsi=\"TestNamespaceXSI\""
      " xmlns:ext=\"TestNamespaceEXT\""
      " id=\"testId\""
      " type=\"static\""
      " availabilityStartTime=\"2015-03-24T1:10:50\""
      " availabilityEndTime=\"2015-03-24T1:10:50\""
      " mediaPresentationDuration=\"P0Y1M2DT12H10M20.5S\""
      " minimumUpdatePeriod=\"P0Y1M2DT12H10M20.5S\""
      " minBufferTime=\"P0Y1M2DT12H10M20.5S\""
      " timeShiftBufferDepth=\"P0Y1M2DT12H10M20.5S\""
      " suggestedPresentationDelay=\"P0Y1M2DT12H10M20.5S\""
      " maxSegmentDuration=\"P0Y1M2DT12H10M20.5S\""
      " maxSubsegmentDuration=\"P0Y1M2DT12H10M20.5S\"></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);

  assert_equals_string (mpdclient->mpd_node->default_namespace,
      "urn:mpeg:dash:schema:mpd:2011");
  assert_equals_string (mpdclient->mpd_node->namespace_xsi, "TestNamespaceXSI");
  assert_equals_string (mpdclient->mpd_node->namespace_ext, "TestNamespaceEXT");
  assert_equals_string (mpdclient->mpd_node->schemaLocation,
      "TestSchemaLocation");
  assert_equals_string (mpdclient->mpd_node->id, "testId");

  assert_equals_int (mpdclient->mpd_node->type, GST_MPD_FILE_TYPE_STATIC);

  availabilityStartTime = mpdclient->mpd_node->availabilityStartTime;
  assert_equals_int (gst_date_time_get_year (availabilityStartTime), 2015);
  assert_equals_int (gst_date_time_get_month (availabilityStartTime), 3);
  assert_equals_int (gst_date_time_get_day (availabilityStartTime), 24);
  assert_equals_int (gst_date_time_get_hour (availabilityStartTime), 1);
  assert_equals_int (gst_date_time_get_minute (availabilityStartTime), 10);
  assert_equals_int (gst_date_time_get_second (availabilityStartTime), 50);

  availabilityEndTime = mpdclient->mpd_node->availabilityEndTime;
  assert_equals_int (gst_date_time_get_year (availabilityEndTime), 2015);
  assert_equals_int (gst_date_time_get_month (availabilityEndTime), 3);
  assert_equals_int (gst_date_time_get_day (availabilityEndTime), 24);
  assert_equals_int (gst_date_time_get_hour (availabilityEndTime), 1);
  assert_equals_int (gst_date_time_get_minute (availabilityEndTime), 10);
  assert_equals_int (gst_date_time_get_second (availabilityEndTime), 50);

  assert_equals_int64 (mpdclient->mpd_node->mediaPresentationDuration,
      (gint64) duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->minimumUpdatePeriod,
      (gint64) duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->minBufferTime,
      (gint64) duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->timeShiftBufferDepth,
      (gint64) duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->suggestedPresentationDelay,
      (gint64) duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->maxSegmentDuration,
      (gint64) duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  assert_equals_int64 (mpdclient->mpd_node->maxSubsegmentDuration,
      (gint64) duration_to_ms (0, 1, 2, 12, 10, 20, 500));

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the ProgramInformation attributes
 *
 */
GST_START_TEST (dash_mpdparser_programInformation)
{
  GstProgramInformationNode *program;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<ProgramInformation lang=\"en\""
      " moreInformationURL=\"TestMoreInformationUrl\">"
      "<Title>TestTitle</Title>"
      "<Source>TestSource</Source>"
      "<Copyright>TestCopyright</Copyright> </ProgramInformation> </MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  program =
      (GstProgramInformationNode *) mpdclient->mpd_node->ProgramInfo->data;
  assert_equals_string (program->lang, "en");
  assert_equals_string (program->moreInformationURL, "TestMoreInformationUrl");
  assert_equals_string (program->Title, "TestTitle");
  assert_equals_string (program->Source, "TestSource");
  assert_equals_string (program->Copyright, "TestCopyright");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the BaseURL attributes
 *
 */
GST_START_TEST (dash_mpdparser_baseURL)
{
  GstBaseURL *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<BaseURL serviceLocation=\"TestServiceLocation\""
      " byteRange=\"TestByteRange\">TestBaseURL</BaseURL></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  baseURL = (GstBaseURL *) mpdclient->mpd_node->BaseURLs->data;
  assert_equals_string (baseURL->baseURL, "TestBaseURL");
  assert_equals_string (baseURL->serviceLocation, "TestServiceLocation");
  assert_equals_string (baseURL->byteRange, "TestByteRange");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the Location attributes
 *
 */
GST_START_TEST (dash_mpdparser_location)
{
  const gchar *location;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Location>TestLocation</Location></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  location = (gchar *) mpdclient->mpd_node->Locations->data;
  assert_equals_string (location, "TestLocation");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Metrics attributes
 *
 */
GST_START_TEST (dash_mpdparser_metrics)
{
  GstMetricsNode *metricsNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Metrics metrics=\"TestMetric\"></Metrics></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  metricsNode = (GstMetricsNode *) mpdclient->mpd_node->Metrics->data;
  assert_equals_string (metricsNode->metrics, "TestMetric");
  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Metrics Range attributes
 *
 */
GST_START_TEST (dash_mpdparser_metrics_range)
{
  GstMetricsNode *metricsNode;
  GstMetricsRangeNode *metricsRangeNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Metrics><Range starttime=\"P0Y1M2DT12H10M20.5S\""
      " duration=\"P0Y1M2DT12H10M20.1234567S\"></Range></Metrics></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  metricsNode = (GstMetricsNode *) mpdclient->mpd_node->Metrics->data;
  assert_equals_pointer (metricsNode->metrics, NULL);
  metricsRangeNode = (GstMetricsRangeNode *) metricsNode->MetricsRanges->data;
  assert_equals_int64 (metricsRangeNode->starttime,
      (gint64) duration_to_ms (0, 1, 2, 12, 10, 20, 500));
  assert_equals_int64 (metricsRangeNode->duration,
      (gint64) duration_to_ms (0, 1, 2, 12, 10, 20, 123));

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Metrics Reporting attributes
 *
 */
GST_START_TEST (dash_mpdparser_metrics_reporting)
{
  GstMetricsNode *metricsNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Metrics><Reporting></Reporting></Metrics></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  metricsNode = (GstMetricsNode *) mpdclient->mpd_node->Metrics->data;
  assert_equals_pointer (metricsNode->metrics, NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period attributes
 *
 */
GST_START_TEST (dash_mpdparser_period)
{
  GstPeriodNode *periodNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period id=\"TestId\" start=\"P0Y1M2DT12H10M20.1234567S\""
      " duration=\"P0Y1M2DT12H10M20.7654321S\""
      " bitstreamSwitching=\"true\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  assert_equals_string (periodNode->id, "TestId");
  assert_equals_int64 (periodNode->start,
      (gint64) duration_to_ms (0, 1, 2, 12, 10, 20, 123));
  assert_equals_int64 (periodNode->duration,
      (gint64) duration_to_ms (0, 1, 2, 12, 10, 20, 765));
  assert_equals_int (periodNode->bitstreamSwitching, 1);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period baseURL attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_baseURL)
{
  GstPeriodNode *periodNode;
  GstBaseURL *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><BaseURL serviceLocation=\"TestServiceLocation\""
      " byteRange=\"TestByteRange\">TestBaseURL</BaseURL></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  baseURL = (GstBaseURL *) periodNode->BaseURLs->data;
  assert_equals_string (baseURL->baseURL, "TestBaseURL");
  assert_equals_string (baseURL->serviceLocation, "TestServiceLocation");
  assert_equals_string (baseURL->byteRange, "TestByteRange");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentBase attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentBase)
{
  GstPeriodNode *periodNode;
  GstSegmentBaseType *segmentBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentBase timescale=\"123456\""
      " presentationTimeOffset=\"123456789\""
      " indexRange=\"100-200\""
      " indexRangeExact=\"true\"></SegmentBase></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentBase = periodNode->SegmentBase;
  assert_equals_uint64 (segmentBase->timescale, 123456);
  assert_equals_uint64 (segmentBase->presentationTimeOffset, 123456789);
  assert_equals_uint64 (segmentBase->indexRange->first_byte_pos, 100);
  assert_equals_uint64 (segmentBase->indexRange->last_byte_pos, 200);
  assert_equals_int (segmentBase->indexRangeExact, 1);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentBase Initialization attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentBase_initialization)
{
  GstPeriodNode *periodNode;
  GstSegmentBaseType *segmentBase;
  GstURLType *initialization;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentBase><Initialisation"
      " sourceURL=\"TestSourceURL\""
      " range=\"100-200\"></Initialisation></SegmentBase></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentBase = periodNode->SegmentBase;
  initialization = segmentBase->Initialization;
  assert_equals_string (initialization->sourceURL, "TestSourceURL");
  assert_equals_uint64 (initialization->range->first_byte_pos, 100);
  assert_equals_uint64 (initialization->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentBase RepresentationIndex attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentBase_representationIndex)
{
  GstPeriodNode *periodNode;
  GstSegmentBaseType *segmentBase;
  GstURLType *representationIndex;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentBase><RepresentationIndex"
      " sourceURL=\"TestSourceURL\""
      " range=\"100-200\"></RepresentationIndex></SegmentBase></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentBase = periodNode->SegmentBase;
  representationIndex = segmentBase->RepresentationIndex;
  assert_equals_string (representationIndex->sourceURL, "TestSourceURL");
  assert_equals_uint64 (representationIndex->range->first_byte_pos, 100);
  assert_equals_uint64 (representationIndex->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentList)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentList></SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  fail_if (segmentList == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList MultipleSegmentBaseType attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentList_multipleSegmentBaseType)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstMultSegmentBaseType *multSegBaseType;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentList duration=\"10\" startNumber=\"11\">"
      "</SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  multSegBaseType = segmentList->MultSegBaseType;
  assert_equals_uint64 (multSegBaseType->duration, 10);
  assert_equals_uint64 (multSegBaseType->startNumber, 11);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList MultipleSegmentBaseType SegmentBaseType
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentBaseType)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentBaseType *segBaseType;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentList timescale=\"10\""
      " presentationTimeOffset=\"11\""
      " indexRange=\"20-21\""
      " indexRangeExact=\"false\"></SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  multSegBaseType = segmentList->MultSegBaseType;
  segBaseType = multSegBaseType->SegBaseType;
  assert_equals_uint64 (segBaseType->timescale, 10);
  assert_equals_uint64 (segBaseType->presentationTimeOffset, 11);
  assert_equals_uint64 (segBaseType->indexRange->first_byte_pos, 20);
  assert_equals_uint64 (segBaseType->indexRange->last_byte_pos, 21);
  assert_equals_int (segBaseType->indexRangeExact, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList MultipleSegmentBaseType SegmentTimeline
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentTimeline)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentTimelineNode *segmentTimeline;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentList><SegmentTimeline "
      " ></SegmentTimeline></SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  multSegBaseType = segmentList->MultSegBaseType;
  segmentTimeline = multSegBaseType->SegmentTimeline;
  fail_if (segmentTimeline == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList MultipleSegmentBaseType SegmentTimeline S
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentTimeline_s)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentTimelineNode *segmentTimeline;
  GstSNode *sNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentList><SegmentTimeline><S t=\"1\" d=\"2\" r=\"3\">"
      "</S></SegmentTimeline></SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  multSegBaseType = segmentList->MultSegBaseType;
  segmentTimeline = multSegBaseType->SegmentTimeline;
  sNode = (GstSNode *) g_queue_peek_head (&segmentTimeline->S);
  assert_equals_uint64 (sNode->t, 1);
  assert_equals_uint64 (sNode->d, 2);
  assert_equals_uint64 (sNode->r, 3);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList MultipleSegmentBaseType BitstreamSwitching
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentList_multipleSegmentBaseType_bitstreamSwitching)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstMultSegmentBaseType *multSegBaseType;
  GstURLType *bitstreamSwitching;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentList><BitstreamSwitching"
      " sourceURL=\"TestSourceURL\" range=\"100-200\""
      "></BitstreamSwitching></SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  multSegBaseType = segmentList->MultSegBaseType;
  bitstreamSwitching = multSegBaseType->BitstreamSwitching;
  assert_equals_string (bitstreamSwitching->sourceURL, "TestSourceURL");
  assert_equals_uint64 (bitstreamSwitching->range->first_byte_pos, 100);
  assert_equals_uint64 (bitstreamSwitching->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentList SegmentURL attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentList_segmentURL)
{
  GstPeriodNode *periodNode;
  GstSegmentListNode *segmentList;
  GstSegmentURLNode *segmentURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentList><SegmentURL"
      " media=\"TestMedia\" mediaRange=\"100-200\""
      " index=\"TestIndex\" indexRange=\"300-400\""
      "></SegmentURL></SegmentList></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentList = periodNode->SegmentList;
  segmentURL = (GstSegmentURLNode *) segmentList->SegmentURL->data;
  assert_equals_string (segmentURL->media, "TestMedia");
  assert_equals_uint64 (segmentURL->mediaRange->first_byte_pos, 100);
  assert_equals_uint64 (segmentURL->mediaRange->last_byte_pos, 200);
  assert_equals_string (segmentURL->index, "TestIndex");
  assert_equals_uint64 (segmentURL->indexRange->first_byte_pos, 300);
  assert_equals_uint64 (segmentURL->indexRange->last_byte_pos, 400);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentTemplate)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentTemplate"
      " media=\"TestMedia\" index=\"TestIndex\""
      " initialization=\"TestInitialization\""
      " bitstreamSwitching=\"TestBitstreamSwitching\""
      "></SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  assert_equals_string (segmentTemplate->media, "TestMedia");
  assert_equals_string (segmentTemplate->index, "TestIndex");
  assert_equals_string (segmentTemplate->initialization, "TestInitialization");
  assert_equals_string (segmentTemplate->bitstreamSwitching,
      "TestBitstreamSwitching");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate MultipleSegmentBaseType attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  GstMultSegmentBaseType *multSegBaseType;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentTemplate"
      " duration=\"10\" startNumber=\"11\""
      "></SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  multSegBaseType = segmentTemplate->MultSegBaseType;
  assert_equals_uint64 (multSegBaseType->duration, 10);
  assert_equals_uint64 (multSegBaseType->startNumber, 11);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate MultipleSegmentBaseType SegmentBaseType
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentBaseType)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentBaseType *segBaseType;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentTemplate timescale=\"123456\""
      " presentationTimeOffset=\"123456789\""
      " indexRange=\"100-200\""
      " indexRangeExact=\"true\"></SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  multSegBaseType = segmentTemplate->MultSegBaseType;
  segBaseType = multSegBaseType->SegBaseType;
  assert_equals_uint64 (segBaseType->timescale, 123456);
  assert_equals_uint64 (segBaseType->presentationTimeOffset, 123456789);
  assert_equals_uint64 (segBaseType->indexRange->first_byte_pos, 100);
  assert_equals_uint64 (segBaseType->indexRange->last_byte_pos, 200);
  assert_equals_int (segBaseType->indexRangeExact, TRUE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate MultipleSegmentBaseType SegmentTimeline
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentTimeline)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentTimelineNode *segmentTimeline;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentTemplate><SegmentTimeline>"
      "</SegmentTimeline></SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  multSegBaseType = segmentTemplate->MultSegBaseType;
  segmentTimeline = (GstSegmentTimelineNode *) multSegBaseType->SegmentTimeline;
  fail_if (segmentTimeline == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate MultipleSegmentBaseType SegmentTimeline
 * S attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentTimeline_s)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  GstMultSegmentBaseType *multSegBaseType;
  GstSegmentTimelineNode *segmentTimeline;
  GstSNode *sNode;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentTemplate><SegmentTimeline><S t=\"1\" d=\"2\" r=\"3\">"
      "</S></SegmentTimeline></SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  multSegBaseType = segmentTemplate->MultSegBaseType;
  segmentTimeline = (GstSegmentTimelineNode *) multSegBaseType->SegmentTimeline;
  sNode = (GstSNode *) g_queue_peek_head (&segmentTimeline->S);
  assert_equals_uint64 (sNode->t, 1);
  assert_equals_uint64 (sNode->d, 2);
  assert_equals_uint64 (sNode->r, 3);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period SegmentTemplate MultipleSegmentBaseType
 * BitstreamSwitching attributes
 */
GST_START_TEST
    (dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_bitstreamSwitching)
{
  GstPeriodNode *periodNode;
  GstSegmentTemplateNode *segmentTemplate;
  GstMultSegmentBaseType *multSegBaseType;
  GstURLType *bitstreamSwitching;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><SegmentTemplate><BitstreamSwitching"
      " sourceURL=\"TestSourceURL\" range=\"100-200\""
      "></BitstreamSwitching></SegmentTemplate></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  segmentTemplate = periodNode->SegmentTemplate;
  multSegBaseType = segmentTemplate->MultSegBaseType;
  bitstreamSwitching = multSegBaseType->BitstreamSwitching;
  assert_equals_string (bitstreamSwitching->sourceURL, "TestSourceURL");
  assert_equals_uint64 (bitstreamSwitching->range->first_byte_pos, 100);
  assert_equals_uint64 (bitstreamSwitching->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet id=\"7\" group=\"8\" lang=\"en\""
      " contentType=\"TestContentType\" par=\"4:3\""
      " minBandwidth=\"100\" maxBandwidth=\"200\""
      " minWidth=\"1000\" maxWidth=\"2000\""
      " minHeight=\"1100\" maxHeight=\"2100\""
      " minFrameRate=\"25/123\" maxFrameRate=\"26\""
      " segmentAlignment=\"2\" subsegmentAlignment=\"false\""
      " subsegmentStartsWithSAP=\"6\""
      " bitstreamSwitching=\"false\"></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  assert_equals_uint64 (adaptationSet->id, 7);
  assert_equals_uint64 (adaptationSet->group, 8);
  assert_equals_string (adaptationSet->lang, "en");
  assert_equals_string (adaptationSet->contentType, "TestContentType");
  assert_equals_uint64 (adaptationSet->par->num, 4);
  assert_equals_uint64 (adaptationSet->par->den, 3);
  assert_equals_uint64 (adaptationSet->minBandwidth, 100);
  assert_equals_uint64 (adaptationSet->maxBandwidth, 200);
  assert_equals_uint64 (adaptationSet->minWidth, 1000);
  assert_equals_uint64 (adaptationSet->maxWidth, 2000);
  assert_equals_uint64 (adaptationSet->minHeight, 1100);
  assert_equals_uint64 (adaptationSet->maxHeight, 2100);
  assert_equals_uint64 (adaptationSet->minFrameRate->num, 25);
  assert_equals_uint64 (adaptationSet->minFrameRate->den, 123);
  assert_equals_uint64 (adaptationSet->maxFrameRate->num, 26);
  assert_equals_uint64 (adaptationSet->maxFrameRate->den, 1);
  assert_equals_int (adaptationSet->segmentAlignment->flag, 1);
  assert_equals_uint64 (adaptationSet->segmentAlignment->value, 2);
  assert_equals_int (adaptationSet->subsegmentAlignment->flag, 0);
  assert_equals_uint64 (adaptationSet->subsegmentAlignment->value, 0);
  assert_equals_int (adaptationSet->subsegmentStartsWithSAP, 6);
  assert_equals_int (adaptationSet->bitstreamSwitching, 0);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet RepresentationBase attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_representationBase)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationBaseType *representationBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      " <Period><AdaptationSet profiles=\"TestProfiles\""
      " width=\"100\" height=\"200\""
      " sar=\"10:20\""
      " frameRate=\"30/40\""
      " audioSamplingRate=\"TestAudioSamplingRate\""
      " mimeType=\"TestMimeType\""
      " segmentProfiles=\"TestSegmentProfiles\""
      " codecs=\"TestCodecs\""
      " maximumSAPPeriod=\"3.4\""
      " startWithSAP=\"0\""
      " maxPlayoutRate=\"1.2\""
      " codingDependency=\"false\""
      " scanType=\"progressive\"></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representationBase = adaptationSet->RepresentationBase;
  assert_equals_string (representationBase->profiles, "TestProfiles");
  assert_equals_uint64 (representationBase->width, 100);
  assert_equals_uint64 (representationBase->height, 200);
  assert_equals_uint64 (representationBase->sar->num, 10);
  assert_equals_uint64 (representationBase->sar->den, 20);
  assert_equals_uint64 (representationBase->frameRate->num, 30);
  assert_equals_uint64 (representationBase->frameRate->den, 40);
  assert_equals_string (representationBase->audioSamplingRate,
      "TestAudioSamplingRate");
  assert_equals_string (representationBase->mimeType, "TestMimeType");
  assert_equals_string (representationBase->segmentProfiles,
      "TestSegmentProfiles");
  assert_equals_string (representationBase->codecs, "TestCodecs");
  assert_equals_float (representationBase->maximumSAPPeriod, 3.4);
  assert_equals_int (representationBase->startWithSAP, GST_SAP_TYPE_0);
  assert_equals_float (representationBase->maxPlayoutRate, 1.2);
  assert_equals_float (representationBase->codingDependency, 0);
  assert_equals_string (representationBase->scanType, "progressive");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet RepresentationBase FramePacking attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representationBase_framePacking) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationBaseType *representationBase;
  GstDescriptorType *framePacking;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><FramePacking"
      " schemeIdUri=\"TestSchemeIdUri\" value=\"TestValue\""
      " ></FramePacking></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representationBase = adaptationSet->RepresentationBase;
  framePacking = (GstDescriptorType *) representationBase->FramePacking->data;
  assert_equals_string (framePacking->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (framePacking->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet RepresentationBase
 * AudioChannelConfiguration attributes
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representationBase_audioChannelConfiguration)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationBaseType *representationBase;
  GstDescriptorType *audioChannelConfiguration;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><AudioChannelConfiguration"
      " schemeIdUri=\"TestSchemeIdUri\" value=\"TestValue\""
      " ></AudioChannelConfiguration></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representationBase = adaptationSet->RepresentationBase;
  audioChannelConfiguration =
      (GstDescriptorType *) representationBase->AudioChannelConfiguration->data;
  assert_equals_string (audioChannelConfiguration->schemeIdUri,
      "TestSchemeIdUri");
  assert_equals_string (audioChannelConfiguration->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet RepresentationBase ContentProtection
 * attributes
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representationBase_contentProtection) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationBaseType *representationBase;
  GstDescriptorType *contentProtection;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><ContentProtection"
      " schemeIdUri=\"TestSchemeIdUri\" value=\"TestValue\""
      " ></ContentProtection></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representationBase = adaptationSet->RepresentationBase;
  contentProtection =
      (GstDescriptorType *) representationBase->ContentProtection->data;
  assert_equals_string (contentProtection->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (contentProtection->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Accessibility attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_accessibility)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstDescriptorType *accessibility;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Accessibility"
      " schemeIdUri=\"TestSchemeIdUri\" value=\"TestValue\""
      " ></Accessibility></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  accessibility = (GstDescriptorType *) adaptationSet->Accessibility->data;
  assert_equals_string (accessibility->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (accessibility->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Role attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_role)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstDescriptorType *role;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Role"
      " schemeIdUri=\"TestSchemeIdUri\" value=\"TestValue\""
      " ></Role></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  role = (GstDescriptorType *) adaptationSet->Role->data;
  assert_equals_string (role->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (role->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Rating attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_rating)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstDescriptorType *rating;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Rating"
      " schemeIdUri=\"TestSchemeIdUri\" value=\"TestValue\""
      " ></Rating></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  rating = (GstDescriptorType *) adaptationSet->Rating->data;
  assert_equals_string (rating->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (rating->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Viewpoint attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_viewpoint)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstDescriptorType *viewpoint;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Viewpoint"
      " schemeIdUri=\"TestSchemeIdUri\" value=\"TestValue\""
      " ></Viewpoint></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  viewpoint = (GstDescriptorType *) adaptationSet->Viewpoint->data;
  assert_equals_string (viewpoint->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (viewpoint->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet ContentComponent attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_contentComponent)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstContentComponentNode *contentComponent;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><ContentComponent"
      " id=\"1\" lang=\"en\" contentType=\"TestContentType\" par=\"10:20\""
      " ></ContentComponent></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  contentComponent = (GstContentComponentNode *)
      adaptationSet->ContentComponents->data;
  assert_equals_uint64 (contentComponent->id, 1);
  assert_equals_string (contentComponent->lang, "en");
  assert_equals_string (contentComponent->contentType, "TestContentType");
  assert_equals_uint64 (contentComponent->par->num, 10);
  assert_equals_uint64 (contentComponent->par->den, 20);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet ContentComponent Accessibility attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_contentComponent_accessibility) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstContentComponentNode *contentComponent;
  GstDescriptorType *accessibility;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><ContentComponent><Accessibility"
      " schemeIdUri=\"TestSchemeIdUri\" value=\"TestValue\""
      " ></Accessibility></ContentComponent></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  contentComponent = (GstContentComponentNode *)
      adaptationSet->ContentComponents->data;
  accessibility = (GstDescriptorType *) contentComponent->Accessibility->data;
  assert_equals_string (accessibility->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (accessibility->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet ContentComponent Role attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_contentComponent_role)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstContentComponentNode *contentComponent;
  GstDescriptorType *role;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><ContentComponent><Role"
      " schemeIdUri=\"TestSchemeIdUri\" value=\"TestValue\""
      " ></Role></ContentComponent></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  contentComponent = (GstContentComponentNode *)
      adaptationSet->ContentComponents->data;
  role = (GstDescriptorType *) contentComponent->Role->data;
  assert_equals_string (role->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (role->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet ContentComponent Rating attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_contentComponent_rating)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstContentComponentNode *contentComponent;
  GstDescriptorType *rating;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><ContentComponent><Rating"
      " schemeIdUri=\"TestSchemeIdUri\" value=\"TestValue\""
      " ></Rating></ContentComponent></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  contentComponent = (GstContentComponentNode *)
      adaptationSet->ContentComponents->data;
  rating = (GstDescriptorType *) contentComponent->Rating->data;
  assert_equals_string (rating->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (rating->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet ContentComponent Viewpoint attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_contentComponent_viewpoint)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstContentComponentNode *contentComponent;
  GstDescriptorType *viewpoint;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><ContentComponent><Viewpoint"
      " schemeIdUri=\"TestSchemeIdUri\" value=\"TestValue\""
      " ></Viewpoint></ContentComponent></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  contentComponent = (GstContentComponentNode *)
      adaptationSet->ContentComponents->data;
  viewpoint = (GstDescriptorType *) contentComponent->Viewpoint->data;
  assert_equals_string (viewpoint->schemeIdUri, "TestSchemeIdUri");
  assert_equals_string (viewpoint->value, "TestValue");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet BaseURL attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_baseURL)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstBaseURL *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><BaseURL"
      " serviceLocation=\"TestServiceLocation\" byteRange=\"TestByteRange\""
      " >TestBaseURL</BaseURL></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  baseURL = (GstBaseURL *) adaptationSet->BaseURLs->data;
  assert_equals_string (baseURL->baseURL, "TestBaseURL");
  assert_equals_string (baseURL->serviceLocation, "TestServiceLocation");
  assert_equals_string (baseURL->byteRange, "TestByteRange");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet SegmentBase attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_segmentBase)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentBaseType *segmentBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><SegmentBase"
      " timescale=\"123456\""
      " presentationTimeOffset=\"123456789\""
      " indexRange=\"100-200\""
      " indexRangeExact=\"true\""
      " ></SegmentBase></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  segmentBase = adaptationSet->SegmentBase;
  assert_equals_uint64 (segmentBase->timescale, 123456);
  assert_equals_uint64 (segmentBase->presentationTimeOffset, 123456789);
  assert_equals_uint64 (segmentBase->indexRange->first_byte_pos, 100);
  assert_equals_uint64 (segmentBase->indexRange->last_byte_pos, 200);
  assert_equals_int (segmentBase->indexRangeExact, TRUE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet SegmentBase Initialization attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_segmentBase_initialization)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentBaseType *segmentBase;
  GstURLType *initialization;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><SegmentBase><Initialisation"
      " sourceURL=\"TestSourceURL\" range=\"100-200\""
      "></Initialisation></SegmentBase></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  segmentBase = adaptationSet->SegmentBase;
  initialization = segmentBase->Initialization;
  assert_equals_string (initialization->sourceURL, "TestSourceURL");
  assert_equals_uint64 (initialization->range->first_byte_pos, 100);
  assert_equals_uint64 (initialization->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet SegmentBase RepresentationIndex attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_segmentBase_representationIndex) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentBaseType *segmentBase;
  GstURLType *representationIndex;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><SegmentBase><RepresentationIndex"
      " sourceURL=\"TestSourceURL\" range=\"100-200\""
      "></RepresentationIndex></SegmentBase></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  segmentBase = adaptationSet->SegmentBase;
  representationIndex = segmentBase->RepresentationIndex;
  assert_equals_string (representationIndex->sourceURL, "TestSourceURL");
  assert_equals_uint64 (representationIndex->range->first_byte_pos, 100);
  assert_equals_uint64 (representationIndex->range->last_byte_pos, 200);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet SegmentList attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_segmentList)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentListNode *segmentList;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><SegmentList"
      "></SegmentList></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  segmentList = adaptationSet->SegmentList;
  fail_if (segmentList == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet SegmentTemplate attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_segmentTemplate)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstSegmentTemplateNode *segmentTemplate;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><SegmentTemplate"
      " media=\"TestMedia\" index=\"TestIndex\""
      " initialization=\"TestInitialization\""
      " bitstreamSwitching=\"TestBitstreamSwitching\""
      "></SegmentTemplate></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  segmentTemplate = adaptationSet->SegmentTemplate;
  assert_equals_string (segmentTemplate->media, "TestMedia");
  assert_equals_string (segmentTemplate->index, "TestIndex");
  assert_equals_string (segmentTemplate->initialization, "TestInitialization");
  assert_equals_string (segmentTemplate->bitstreamSwitching,
      "TestBitstreamSwitching");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_representation)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Representation"
      " id=\"Test Id\""
      " bandwidth=\"100\""
      " qualityRanking=\"200\""
      " dependencyId=\"one two three\""
      " mediaStreamStructureId=\"\""
      "></Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  assert_equals_string (representation->id, "Test Id");
  assert_equals_uint64 (representation->bandwidth, 100);
  assert_equals_uint64 (representation->qualityRanking, 200);
  assert_equals_string (representation->dependencyId[0], "one");
  assert_equals_string (representation->dependencyId[1], "two");
  assert_equals_string (representation->dependencyId[2], "three");
  assert_equals_pointer (representation->dependencyId[3], NULL);
  assert_equals_pointer (representation->mediaStreamStructureId[0], NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation RepresentationBaseType attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representation_representationBase) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstRepresentationBaseType *representationBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Representation"
      "></Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  representationBase = (GstRepresentationBaseType *)
      representation->RepresentationBase;
  fail_if (representationBase == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation BaseURL attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_representation_baseURL)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstBaseURL *baseURL;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Representation><BaseURL"
      " serviceLocation=\"TestServiceLocation\" byteRange=\"TestByteRange\""
      " >TestBaseURL</BaseURL></Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  baseURL = (GstBaseURL *) representation->BaseURLs->data;
  assert_equals_string (baseURL->baseURL, "TestBaseURL");
  assert_equals_string (baseURL->serviceLocation, "TestServiceLocation");
  assert_equals_string (baseURL->byteRange, "TestByteRange");

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation SubRepresentation attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representation_subRepresentation) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSubRepresentationNode *subRepresentation;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Representation><SubRepresentation"
      " level=\"100\""
      " dependencyLevel=\"1 2 3\""
      " bandwidth=\"200\""
      " contentComponent=\"content1 content2\""
      " ></SubRepresentation></Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  subRepresentation = (GstSubRepresentationNode *)
      representation->SubRepresentations->data;
  assert_equals_uint64 (subRepresentation->level, 100);
  assert_equals_uint64 (subRepresentation->size, 3);
  assert_equals_uint64 (subRepresentation->dependencyLevel[0], 1);
  assert_equals_uint64 (subRepresentation->dependencyLevel[1], 2);
  assert_equals_uint64 (subRepresentation->dependencyLevel[2], 3);
  assert_equals_uint64 (subRepresentation->bandwidth, 200);
  assert_equals_string (subRepresentation->contentComponent[0], "content1");
  assert_equals_string (subRepresentation->contentComponent[1], "content2");
  assert_equals_pointer (subRepresentation->contentComponent[2], NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation SubRepresentation
 * RepresentationBase attributes
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representation_subRepresentation_representationBase)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSubRepresentationNode *subRepresentation;
  GstRepresentationBaseType *representationBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Representation><SubRepresentation"
      " ></SubRepresentation></Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  subRepresentation = (GstSubRepresentationNode *)
      representation->SubRepresentations->data;
  representationBase = (GstRepresentationBaseType *)
      subRepresentation->RepresentationBase;
  fail_if (representationBase == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation SegmentBase attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_representation_segmentBase)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSegmentBaseType *segmentBase;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Representation><SegmentBase"
      " ></SegmentBase></Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  segmentBase = representation->SegmentBase;
  fail_if (segmentBase == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation SegmentList attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_adaptationSet_representation_segmentList)
{
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSegmentListNode *segmentList;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Representation><SegmentList"
      " ></SegmentList></Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  segmentList = representation->SegmentList;
  fail_if (segmentList == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period AdaptationSet Representation SegmentTemplate attributes
 *
 */
GST_START_TEST
    (dash_mpdparser_period_adaptationSet_representation_segmentTemplate) {
  GstPeriodNode *periodNode;
  GstAdaptationSetNode *adaptationSet;
  GstRepresentationNode *representation;
  GstSegmentTemplateNode *segmentTemplate;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><AdaptationSet><Representation><SegmentTemplate"
      " ></SegmentTemplate></Representation></AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  adaptationSet = (GstAdaptationSetNode *) periodNode->AdaptationSets->data;
  representation = (GstRepresentationNode *)
      adaptationSet->Representations->data;
  segmentTemplate = representation->SegmentTemplate;
  fail_if (segmentTemplate == NULL);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing Period Subset attributes
 *
 */
GST_START_TEST (dash_mpdparser_period_subset)
{
  GstPeriodNode *periodNode;
  GstSubsetNode *subset;
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period><Subset contains=\"1 2 3\"></Subset></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  periodNode = (GstPeriodNode *) mpdclient->mpd_node->Periods->data;
  subset = (GstSubsetNode *) periodNode->Subsets->data;
  assert_equals_uint64 (subset->size, 3);
  assert_equals_uint64 (subset->contains[0], 1);
  assert_equals_uint64 (subset->contains[1], 2);
  assert_equals_uint64 (subset->contains[2], 3);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing the type property: value "dynamic"
 *
 */
GST_START_TEST (dash_mpdparser_type_dynamic)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD type=\"dynamic\" xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\"> </MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);
  assert_equals_int (mpdclient->mpd_node->type, GST_MPD_FILE_TYPE_DYNAMIC);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Validate gst_mpdparser_build_URL_from_template function
 *
 */
GST_START_TEST (dash_mpdparser_template_parsing)
{
  const gchar *url_template;
  const gchar *id = "TestId";
  guint number = 7;
  guint bandwidth = 2500;
  guint64 time = 100;
  gchar *result;

  url_template = "TestMedia$Bandwidth$$$test";
  result =
      gst_mpdparser_build_URL_from_template (url_template, id, number,
      bandwidth, time);
  assert_equals_string (result, "TestMedia2500$test");
  g_free (result);

}

GST_END_TEST;

/*
 * Test handling Representation selection
 *
 */
GST_START_TEST (dash_mpdparser_representation_selection)
{
  GList *adaptationSets;
  GstAdaptationSetNode *adaptationSetNode;
  GList *representations;
  gint represendationIndex;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\">"
      "<Period id=\"Period0\" duration=\"P0Y0M1DT1H1M1S\">"
      "<AdaptationSet id=\"1\" mimeType=\"video/mp4\">"
      "<Representation id=\"v0\" bandwidth=\"500000\"></Representation>"
      "<Representation id=\"v1\" bandwidth=\"250000\"></Representation>"
      "</AdaptationSet></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));
  assert_equals_int (ret, TRUE);

  /* process the xml data */
  ret = gst_mpd_client_setup_media_presentation (mpdclient);
  assert_equals_int (ret, TRUE);

  adaptationSets = gst_mpd_client_get_adaptation_sets (mpdclient);
  fail_if (adaptationSets == NULL);

  adaptationSetNode = adaptationSets->data;
  fail_if (adaptationSetNode == NULL);
  assert_equals_int (adaptationSetNode->id, 1);

  representations = adaptationSetNode->Representations;
  fail_if (representations == NULL);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_min_bandwidth (representations);
  assert_equals_int (represendationIndex, 1);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 0);
  assert_equals_int (represendationIndex, 1);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 100000);
  assert_equals_int (represendationIndex, -1);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 300000);
  assert_equals_int (represendationIndex, 1);

  represendationIndex =
      gst_mpdparser_get_rep_idx_with_max_bandwidth (representations, 500000);
  assert_equals_int (represendationIndex, 0);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing empty xml string
 *
 */
GST_START_TEST (dash_mpdparser_missing_xml)
{
  const gchar *xml = "";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing an xml with no mpd tag
 *
 */
GST_START_TEST (dash_mpdparser_missing_mpd)
{
  const gchar *xml = "<?xml version=\"1.0\"?>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing an MPD with a wrong end tag
 */
GST_START_TEST (dash_mpdparser_no_end_tag)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\"> </NPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test parsing an MPD with no default namespace
 */
GST_START_TEST (dash_mpdparser_no_default_namespace)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD profiles=\"urn:mpeg:dash:profile:isoff-main:2011\"></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, strlen (xml));

  assert_equals_int (ret, TRUE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling wrong period duration during attempts to
 * infer a period duration from the start time of the next period
 */
GST_START_TEST (dash_mpdparser_wrong_period_duration_inferred_from_next_period)
{
  const gchar *periodName;

  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      " availabilityStartTime=\"2015-03-24T0:0:0\""
      " mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "<Period id=\"Period0\" duration=\"P0Y0M0DT1H1M0S\"></Period>"
      "<Period id=\"Period1\"></Period>"
      "<Period id=\"Period2\" start=\"P0Y0M0DT0H0M10S\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);

  /* period_idx should be 0 and we should have no active periods */
  assert_equals_uint64 (mpdclient->period_idx, 0);
  fail_unless (mpdclient->periods == NULL);

  /* process the xml data */
  ret = gst_mpd_client_setup_media_presentation (mpdclient);
  assert_equals_int (ret, TRUE);

  /* Period0 should be present */
  fail_unless (mpdclient->periods != NULL);
  periodName = gst_mpd_client_get_period_id (mpdclient);
  assert_equals_string (periodName, "Period0");

  /* Period1 should not be present due to wrong duration */
  ret = gst_mpd_client_set_period_index (mpdclient, 1);
  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * Test handling wrong period duration during attempts to
 * infer a period duration from the mediaPresentationDuration
 */
GST_START_TEST
    (dash_mpdparser_wrong_period_duration_inferred_from_next_mediaPresentationDuration)
{
  const gchar *xml =
      "<?xml version=\"1.0\"?>"
      "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\""
      " profiles=\"urn:mpeg:dash:profile:isoff-main:2011\""
      " availabilityStartTime=\"2015-03-24T0:0:0\""
      " mediaPresentationDuration=\"P0Y0M0DT3H3M30S\">"
      "<Period id=\"Period0\" start=\"P0Y0M0DT4H0M0S\"></Period></MPD>";

  gboolean ret;
  GstMpdClient *mpdclient = gst_mpd_client_new ();

  ret = gst_mpd_parse (mpdclient, xml, (gint) strlen (xml));

  assert_equals_int (ret, TRUE);

  /* period_idx should be 0 and we should have no active periods */
  assert_equals_uint64 (mpdclient->period_idx, 0);
  fail_unless (mpdclient->periods == NULL);

  /* process the xml data
   * should fail due to wrong duration in Period0 (start > mediaPresentationDuration)
   */
  ret = gst_mpd_client_setup_media_presentation (mpdclient);
  assert_equals_int (ret, FALSE);

  gst_mpd_client_free (mpdclient);
}

GST_END_TEST;

/*
 * create a test suite containing all dash testcases
 */
static Suite *
dash_suite (void)
{
  Suite *s = suite_create ("dash");
  TCase *tc_simpleMPD = tcase_create ("simpleMPD");
  TCase *tc_complexMPD = tcase_create ("complexMPD");
  TCase *tc_negativeTests = tcase_create ("negativeTests");

  GST_DEBUG_CATEGORY_INIT (gst_dash_demux_debug, "gst_dash_demux_debug", 0,
      "mpeg dash tests");

  /* test parsing the simplest possible mpd */
  tcase_add_test (tc_simpleMPD, dash_mpdparser_validsimplempd);

  /* tests parsing attributes from each element type */
  tcase_add_test (tc_simpleMPD, dash_mpdparser_mpd);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_programInformation);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_baseURL);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_location);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_metrics);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_metrics_range);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_metrics_reporting);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_baseURL);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_segmentBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentBase_initialization);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentBase_representationIndex);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_segmentList);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentList_multipleSegmentBaseType);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentBaseType);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentTimeline);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentList_multipleSegmentBaseType_segmentTimeline_s);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentList_multipleSegmentBaseType_bitstreamSwitching);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_segmentList_segmentURL);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_segmentTemplate);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentBaseType);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentTimeline);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_segmentTimeline_s);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_segmentTemplate_multipleSegmentBaseType_bitstreamSwitching);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_adaptationSet);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representationBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representationBase_framePacking);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representationBase_audioChannelConfiguration);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representationBase_contentProtection);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_accessibility);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_adaptationSet_role);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_adaptationSet_rating);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_adaptationSet_viewpoint);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_contentComponent);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_contentComponent_accessibility);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_contentComponent_role);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_contentComponent_rating);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_contentComponent_viewpoint);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_adaptationSet_baseURL);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_segmentBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_segmentBase_initialization);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_segmentBase_representationIndex);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_segmentList);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_segmentTemplate);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_representationBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_baseURL);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_subRepresentation);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_subRepresentation_representationBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_segmentBase);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_segmentList);
  tcase_add_test (tc_simpleMPD,
      dash_mpdparser_period_adaptationSet_representation_segmentTemplate);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_period_subset);

  /* tests checking other possible values for attributes */
  tcase_add_test (tc_simpleMPD, dash_mpdparser_type_dynamic);
  tcase_add_test (tc_simpleMPD, dash_mpdparser_template_parsing);

  tcase_add_test (tc_complexMPD, dash_mpdparser_representation_selection);
  /* tests checking the parsing of missing/incomplete attributes of xml */
  tcase_add_test (tc_negativeTests, dash_mpdparser_missing_xml);
  tcase_add_test (tc_negativeTests, dash_mpdparser_missing_mpd);
  tcase_add_test (tc_negativeTests, dash_mpdparser_no_end_tag);
  tcase_add_test (tc_negativeTests, dash_mpdparser_no_default_namespace);
  tcase_add_test (tc_negativeTests,
      dash_mpdparser_wrong_period_duration_inferred_from_next_period);
  tcase_add_test (tc_negativeTests,
      dash_mpdparser_wrong_period_duration_inferred_from_next_mediaPresentationDuration);

  suite_add_tcase (s, tc_simpleMPD);
  suite_add_tcase (s, tc_complexMPD);
  suite_add_tcase (s, tc_negativeTests);

  return s;
}

GST_CHECK_MAIN (dash);
