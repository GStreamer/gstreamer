/* GStreamer MythTV Plug-in
 * Copyright (C) <2006> Rosfran Borges <rosfran.borges@indt.org.br>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * When using the LiveTV content, put the location URI in the following
 * format:
 * 
 * 	myth://mythtv:mythtv@xxx.xxx.xxx.xxx:6543/?mythconverg
 * 
 * Where the first field is the protocol (myth), the second and third are user 
 * name (mythtv) and password (mythtv), then backend host name and port number, 
 * and the last field is the database name (mythconverg).
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmythtvsrc.h"
#include <gmyth/gmyth_file_transfer.h>
#include <gmyth/gmyth_livetv.h>

#include <gmyth/gmyth_socket.h>
#include <gmyth/gmyth_tvchain.h>

#include <string.h>
#include <unistd.h>

GST_DEBUG_CATEGORY_STATIC (mythtvsrc_debug);
#define GST_CAT_DEFAULT mythtvsrc_debug

#define GST_GMYTHTV_ID_NUM			            1

#define GST_GMYTHTV_CHANNEL_DEFAULT_NUM		(-1)

#define GMYTHTV_VERSION_DEFAULT			        30

#define GMYTHTV_TRANSFER_MAX_WAITS		     100

#define GMYTHTV_TRANSFER_MAX_RESENDS	       2

#define GMYTHTV_TRANSFER_MAX_BUFFER		(128*1024)

#define MAX_READ_SIZE              		(4*1024)

#define GST_FLOW_ERROR_NO_DATA  			(-101)

#define REQUEST_MAX_SIZE							(64*1024)

#define INTERNAL_BUFFER_SIZE					(90*1024)

static const GstElementDetails gst_mythtv_src_details =
GST_ELEMENT_DETAILS ("MythTV client source",
    "Source/Network",
    "Control and receive data as a client over the network "
    "via raw socket connections using the MythTV protocol",
    "Rosfran Borges <rosfran.borges@indt.org.br>");

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-nuv"));

enum
{
  PROP_0,
  PROP_LOCATION,
#ifndef GST_DISABLE_GST_DEBUG
  PROP_GMYTHTV_DBG,
#endif
  PROP_GMYTHTV_VERSION,
  PROP_GMYTHTV_LIVE,
  PROP_GMYTHTV_LIVEID,
  PROP_GMYTHTV_LIVE_CHAINID,
  PROP_GMYTHTV_ENABLE_TIMING_POSITION,
  PROP_GMYTHTV_CHANNEL_NUM
};

static void gst_mythtv_src_finalize (GObject * gobject);

static GstFlowReturn gst_mythtv_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);

static gboolean gst_mythtv_src_start (GstBaseSrc * bsrc);
static gboolean gst_mythtv_src_stop (GstBaseSrc * bsrc);
static gboolean gst_mythtv_src_get_size (GstBaseSrc * bsrc, guint64 * size);
static gboolean gst_mythtv_src_is_seekable (GstBaseSrc * push_src);

static gboolean gst_mythtv_src_do_seek (GstBaseSrc * base,
    GstSegment * segment);

static gboolean gst_mythtv_src_next_program_chain (GstMythtvSrc * src);

static GstStateChangeReturn
gst_mythtv_src_change_state (GstElement * element, GstStateChange transition);

static void gst_mythtv_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mythtv_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_mythtv_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static gboolean gst_mythtv_src_handle_query (GstPad * pad, GstQuery * query);

static gboolean gst_mythtv_src_handle_event (GstPad * pad, GstEvent * event);

static gint do_read_request_response (GstMythtvSrc * src, guint size,
    GByteArray * data_ptr);

static void
_urihandler_init (GType type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_mythtv_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (mythtvsrc_debug, "mythtvsrc", 0, "MythTV src");
}

GST_BOILERPLATE_FULL (GstMythtvSrc, gst_mythtv_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, _urihandler_init)

     static void gst_mythtv_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (element_class, &gst_mythtv_src_details);

  element_class->change_state = gst_mythtv_src_change_state;

}

static void
gst_mythtv_src_class_init (GstMythtvSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstPushSrcClass *gstpushsrc_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_mythtv_src_set_property;
  gobject_class->get_property = gst_mythtv_src_get_property;
  gobject_class->finalize = gst_mythtv_src_finalize;

  g_object_class_install_property
      (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "The location. In the form:"
          "\n\t\t\tmyth://a.com/file.nuv"
          "\n\t\t\tmyth://a.com:23223/file.nuv "
          "\n\t\t\ta.com/file.nuv - default scheme 'myth'",
          "", G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_VERSION,
      g_param_spec_int ("mythtv-version", "mythtv-version",
          "Change MythTV version", 26, 30, 26, G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_LIVEID,
      g_param_spec_int ("mythtv-live-id", "mythtv-live-id",
          "Change MythTV version",
          0, 200, GST_GMYTHTV_ID_NUM, G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_LIVE_CHAINID,
      g_param_spec_string ("mythtv-live-chainid", "mythtv-live-chainid",
          "Sets the MythTV chain ID (from TV Chain)", "", G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_LIVE,
      g_param_spec_boolean ("mythtv-live", "mythtv-live",
          "Enable MythTV Live TV content streaming", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_ENABLE_TIMING_POSITION,
      g_param_spec_boolean ("mythtv-enable-timing-position",
          "mythtv-enable-timing-position",
          "Enable MythTV Live TV content size continuous updating", FALSE,
          G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_CHANNEL_NUM,
      g_param_spec_int ("mythtv-channel", "mythtv-channel",
          "Change MythTV channel number",
          -1, 99999, GST_GMYTHTV_CHANNEL_DEFAULT_NUM, G_PARAM_READWRITE));

#ifndef GST_DISABLE_GST_DEBUG
  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_DBG,
      g_param_spec_boolean ("mythtv-debug", "mythtv-debug",
          "Enable MythTV debug messages", FALSE, G_PARAM_READWRITE));
#endif

  gstbasesrc_class->start = gst_mythtv_src_start;
  gstbasesrc_class->stop = gst_mythtv_src_stop;
  gstbasesrc_class->get_size = gst_mythtv_src_get_size;
  gstbasesrc_class->is_seekable = gst_mythtv_src_is_seekable;

  gstbasesrc_class->do_seek = gst_mythtv_src_do_seek;
  gstpushsrc_class->create = gst_mythtv_src_create;

  GST_DEBUG_CATEGORY_INIT (mythtvsrc_debug, "mythtvsrc", 0,
      "MythTV Client Source");
}

static void
gst_mythtv_src_init (GstMythtvSrc * this, GstMythtvSrcClass * g_class)
{
  this->file_transfer = NULL;

  this->unique_setup = FALSE;

  this->mythtv_version = GMYTHTV_VERSION_DEFAULT;

  this->state = GST_MYTHTV_SRC_FILE_TRANSFER;

  this->bytes_read = 0;

  this->prev_content_size = 0;

  this->content_size = 0;
  this->read_offset = 0;

  this->content_size_last = 0;

  this->live_tv = FALSE;

  this->enable_timing_position = FALSE;
  this->update_prog_chain = FALSE;

  this->user_agent = g_strdup ("mythtvsrc");
  this->mythtv_caps = NULL;
  this->update_prog_chain = FALSE;

  this->channel_num = GST_GMYTHTV_CHANNEL_DEFAULT_NUM;

  this->eos = FALSE;

  this->bytes_queue = NULL;

  this->wait_to_transfer = 0;

  gst_base_src_set_format (GST_BASE_SRC (this), GST_FORMAT_BYTES);

  gst_pad_set_event_function (GST_BASE_SRC_PAD (GST_BASE_SRC (this)),
      gst_mythtv_src_handle_event);
  gst_pad_set_query_function (GST_BASE_SRC_PAD (GST_BASE_SRC (this)),
      gst_mythtv_src_handle_query);

}

static void
gst_mythtv_src_finalize (GObject * gobject)
{
  GstMythtvSrc *this = GST_MYTHTV_SRC (gobject);

  if (this->mythtv_caps) {
    gst_caps_unref (this->mythtv_caps);
    this->mythtv_caps = NULL;
  }

  if (this->file_transfer) {
    g_object_unref (this->file_transfer);
    this->file_transfer = NULL;
  }

  if (this->spawn_livetv) {
    g_object_unref (this->spawn_livetv);
    this->spawn_livetv = NULL;
  }

  if (this->backend_info) {
    g_object_unref (this->backend_info);
    this->backend_info = NULL;
  }

  if (this->uri_name) {
    g_free (this->uri_name);
  }

  if (this->user_agent) {
    g_free (this->user_agent);
  }

  if (this->bytes_queue) {
    g_byte_array_free (this->bytes_queue, TRUE);
    this->bytes_queue = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static gint
do_read_request_response (GstMythtvSrc * src, guint size, GByteArray * data_ptr)
{
  gint read = 0;
  guint sizetoread = size;
  gint max_iters = GMYTHTV_TRANSFER_MAX_RESENDS;

  GST_LOG_OBJECT (src, "Starting: [%s] Reading %d bytes...", __FUNCTION__,
      sizetoread);

  /* Loop sending the Myth File Transfer request:
   * Retry whilst authentication fails and we supply it. */
  gint len = 0;

  while (sizetoread == size && --max_iters > 0) {

    len = gmyth_file_transfer_read (src->file_transfer,
        data_ptr, sizetoread, TRUE);

    if (len > 0) {
      read += len;
      sizetoread -= len;
    } else if (len < 0) {

      if (src->live_tv == FALSE) {
        read = -1;
        goto eos;
      } else {
        if (len == GMYTHTV_FILE_TRANSFER_READ_ERROR) {  /* -314 */
          GST_INFO_OBJECT (src, "[%s] [LiveTV] FileTransfer READ_ERROR!",
              __FUNCTION__);
          goto done;
        } else if (len == GMYTHTV_FILE_TRANSFER_NEXT_PROG_CHAIN) {      /* -315 */
          GST_INFO_OBJECT (src,
              "[%s] [LiveTV] FileTransfer - Go to the next program chain!",
              __FUNCTION__);
          continue;
        }
        goto done;
      }

    } else if (len == 0)
      goto done;

    if (read == sizetoread)
      goto done;
  }

  if ((read < 0 && !src->live_tv) || max_iters == 0)
    goto eos;

  goto done;

eos:
  src->eos = TRUE;

done:
  return read;
}

static GstFlowReturn
gst_mythtv_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstMythtvSrc *src;
  GstFlowReturn ret = GST_FLOW_OK;
  gint read = -1;

  src = GST_MYTHTV_SRC (psrc);

  /* The caller should know the number of bytes and not read beyond EOS. */
  if (G_UNLIKELY (src->eos))
    goto eos;
  if (G_UNLIKELY (src->update_prog_chain))
    goto change_progchain;

  GST_DEBUG_OBJECT (src, "[%s] offset = %llu, size = %d...", __FUNCTION__,
      src->read_offset, MAX_READ_SIZE);

  GST_DEBUG_OBJECT (src, "[%s]Create: buffer_remain: %d, buffer_size = %d.",
      __FUNCTION__, (gint) src->buffer_remain, src->bytes_queue->len);

  /* just get from the byte array, no network effort... */
  if ((src->buffer_remain = src->bytes_queue->len) < MAX_READ_SIZE) {
    GByteArray *buffer = NULL;
    guint buffer_size_inter = (INTERNAL_BUFFER_SIZE - src->buffer_remain);

    if (buffer_size_inter > REQUEST_MAX_SIZE)
      buffer_size_inter = REQUEST_MAX_SIZE;

    buffer = g_byte_array_new ();

    read = do_read_request_response (src, buffer_size_inter, buffer);

    if (G_UNLIKELY (read < 0)) {
      if (src->live_tv)
        goto change_progchain;
      else
        goto read_error;
    } else if (G_UNLIKELY (read == 0)) {
      if (!src->live_tv)
        goto eos;
      else
        goto done;
    }

    if (G_UNLIKELY (src->update_prog_chain))
      goto change_progchain;

    src->bytes_queue =
        g_byte_array_append (src->bytes_queue, buffer->data, read);
    if (read > buffer_size_inter)
      GST_WARNING_OBJECT (src,
          "[%s]INCREASED buffer size! Backend sent more than we ask him... (%d)",
          __FUNCTION__, abs (read - buffer_size_inter));

    src->buffer_remain += read;

    if (buffer != NULL) {
      g_byte_array_free (buffer, TRUE);
      buffer = NULL;
    }

    GST_DEBUG_OBJECT (src,
        "[%s]BYTES READ (actual) = %d, BYTES READ (cumulative) = %llu, "
        "OFFSET = %llu, CONTENT SIZE = %llu.", __FUNCTION__, read,
        src->bytes_read, src->read_offset, src->content_size);

  }

  guint buffer_size =
      (src->buffer_remain < MAX_READ_SIZE) ? src->buffer_remain : MAX_READ_SIZE;

  *outbuf = gst_buffer_new ();

  /* gets the first buffer_size bytes from the byte array buffer variable */
  /* guint8 *buf = g_memdup( src->bytes_queue->data, buffer_size ); */

  GST_DEBUG_OBJECT (src, "[%s] read from network? %s!, buffer_remain = %d",
      __FUNCTION__,
      read == -1 ? "NO, got from buffer" : "YES, go see the backend's log file",
      src->buffer_remain);

  GST_BUFFER_SIZE (*outbuf) = buffer_size;
  GST_BUFFER_MALLOCDATA (*outbuf) = g_malloc0 (GST_BUFFER_SIZE (*outbuf));
  GST_BUFFER_DATA (*outbuf) = GST_BUFFER_MALLOCDATA (*outbuf);
  g_memmove (GST_BUFFER_DATA ((*outbuf)), src->bytes_queue->data,
      GST_BUFFER_SIZE (*outbuf));
  GST_BUFFER_OFFSET (*outbuf) = src->read_offset;
  GST_BUFFER_OFFSET_END (*outbuf) =
      src->read_offset + GST_BUFFER_SIZE (*outbuf);

  src->buffer_remain -= GST_BUFFER_SIZE (*outbuf);

  src->read_offset += GST_BUFFER_SIZE (*outbuf);
  src->bytes_read += GST_BUFFER_SIZE (*outbuf);
  GST_DEBUG_OBJECT (src, "[%s]Buffer output with size: %d", __FUNCTION__,
      GST_BUFFER_SIZE (*outbuf));

  /* flushs the newly buffer got from byte array */
  src->bytes_queue =
      g_byte_array_remove_range (src->bytes_queue, 0, buffer_size);

  GST_DEBUG_OBJECT (src, "Got buffer: [%s]BUFFER --->SIZE = %d, OFFSET = %llu, "
      "OFFSET_END = %llu.", __FUNCTION__, GST_BUFFER_SIZE (*outbuf),
      GST_BUFFER_OFFSET (*outbuf), GST_BUFFER_OFFSET_END (*outbuf));

  GST_DEBUG_OBJECT (src, "[%s]CONTENT_SIZE = %llu, BYTES_READ = %llu.",
      __FUNCTION__, src->content_size, src->bytes_read);

  if (G_UNLIKELY (src->eos) || (!src->live_tv
          && (src->bytes_read >= src->content_size)))
    goto eos;

done:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_DEBUG_OBJECT (src, "DONE task, reason %s", reason);
    return ret;
  }
eos:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_DEBUG_OBJECT (src, "pausing task, reason %s", reason);
    return GST_FLOW_UNEXPECTED;
  }
  /* ERRORS */
read_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        (NULL), ("Could not read any bytes (%i, %s)", read, src->uri_name));
    return GST_FLOW_ERROR;
  }
change_progchain:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        (NULL), ("Seek failed, go to the next program info... (%i, %s)", read,
            src->uri_name));

    gst_pad_push_event (GST_BASE_SRC_PAD (GST_BASE_SRC (psrc)),
        gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_TIME, 0, -1, 0));
    /* go to the next program chain */
    src->unique_setup = FALSE;
    src->update_prog_chain = TRUE;

    gst_mythtv_src_next_program_chain (src);

    return GST_FLOW_ERROR_NO_DATA;
  }

}

gint64
gst_mythtv_src_get_position (GstMythtvSrc * src)
{

  gint64 size_tmp = 0;
  guint max_tries = 2;

  if (src->live_tv == TRUE && (abs (src->content_size - src->bytes_read) <
          GMYTHTV_TRANSFER_MAX_BUFFER)) {

  get_file_pos:
    g_usleep (10);
    size_tmp = gmyth_recorder_get_file_position (src->spawn_livetv->recorder);
    if (size_tmp > (src->content_size + GMYTHTV_TRANSFER_MAX_BUFFER))
      src->content_size = size_tmp;
    else if (size_tmp > 0 && --max_tries > 0)
      goto get_file_pos;
    GST_LOG_OBJECT (src, "[%s] GET_POSITION: file_position = %lld",
        __FUNCTION__, size_tmp);
    /* sets the last content size amount before it can be updated */
    src->prev_content_size = src->content_size;
  }

  return src->content_size;

}

static gboolean
gst_mythtv_src_do_seek (GstBaseSrc * base, GstSegment * segment)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (base);
  gint64 new_offset = -1;
  gint64 actual_seek = segment->start;
  gboolean ret = TRUE;

  GST_LOG_OBJECT (src, "[%s]DO Seek called! (start = %lld, stop = %lld)",
      __FUNCTION__, segment->start, segment->stop);

  if (segment->format == GST_FORMAT_TIME) {
    goto done;
  }
  GST_LOG_OBJECT (src,
      "[%s]Trying to seek at the value (actual_seek = %lld, read_offset = %lld)",
      __FUNCTION__, actual_seek, src->read_offset);
  /* verify if it needs to seek */
  if (src->read_offset != actual_seek) {

    new_offset =
        gmyth_file_transfer_seek (src->file_transfer, segment->start, SEEK_SET);

    GST_LOG_OBJECT (src,
        "[%s] Segment offset start = %lld, SRC Offset = %lld, NEW actual backend SEEK Offset = %lld.",
        __FUNCTION__, segment->start, src->read_offset, new_offset);
    if (G_UNLIKELY (new_offset < 0)) {
      ret = FALSE;
      if (src->live_tv)
        goto change_progchain;
      else
        goto eos;
    }

    src->read_offset = new_offset;

    if (ret == FALSE) {
      GST_INFO_OBJECT (src, "[%s] Failed to set the SEEK on segment!",
          __FUNCTION__);
    }

  }

done:
  return ret;

eos:
  {

    GST_DEBUG_OBJECT (src, "EOS found on seeking!!!");
    return FALSE;
  }
change_progchain:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        (NULL), ("Seek failed, go to the next program info... (%i, %s)", read,
            src->uri_name));

    gst_pad_push_event (GST_BASE_SRC_PAD (base),
        gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_TIME, 0, -1, 0));
    /* go to the next program chain */
    src->unique_setup = FALSE;
    src->update_prog_chain = TRUE;

    gst_mythtv_src_next_program_chain (src);

    return TRUE;
  }

}

/* create a socket for connecting to remote server */
static gboolean
gst_mythtv_src_start (GstBaseSrc * bsrc)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (bsrc);

  GString *chain_id_local = NULL;

  gboolean ret = TRUE;

  if (G_UNLIKELY (src->update_prog_chain))
    goto change_progchain;

  if (src->unique_setup == FALSE) {
    src->unique_setup = TRUE;
  } else {
    goto done;
  }

  src->backend_info = gmyth_backend_info_new_with_uri (src->uri_name);
  /* testing UPnP... */
  /* gmyth_backend_info_set_hostname( src->backend_info, NULL ); */
  if (src->live_tv) {
    src->spawn_livetv = gmyth_livetv_new ();

    if (src->channel_num != GST_GMYTHTV_CHANNEL_DEFAULT_NUM) {
      if (gmyth_livetv_channel_setup (src->spawn_livetv, src->channel_num,
              src->backend_info) == FALSE) {
        GST_INFO_OBJECT (src, "[%s] LiveTV setup felt down on error!!",
            __FUNCTION__);
        ret = FALSE;
        goto init_failed;
      }
    } else {
      if (gmyth_livetv_setup (src->spawn_livetv, src->backend_info) == FALSE) {
        GST_INFO_OBJECT (src, "[%s] LiveTV setup felt down on error!!",
            __FUNCTION__);
        ret = FALSE;
        goto init_failed;
      }
    }

    src->file_transfer = gmyth_livetv_create_file_transfer (src->spawn_livetv);

    if (NULL == src->file_transfer) {
      GST_INFO_OBJECT (src, "[%s] [LiveTV] FileTransfer equals to NULL!!!",
          __FUNCTION__);
      ret = FALSE;
      goto init_failed;
    }
  } else {

    src->file_transfer = gmyth_file_transfer_new (src->backend_info);

    ret = gmyth_file_transfer_open (src->file_transfer, src->uri_name);

  }

  if (NULL == src->file_transfer) {
    GST_INFO_OBJECT (src, "[%s] FileTransfer equals to NULL!!!", __FUNCTION__);
    goto init_failed;
  }
  /*GST_INFO_OBJECT( src, "[%s] uri = %s.", __FUNCTION__, src->spawn_livetv->file_transfer ); */

  if (src->live_tv == TRUE && ret == TRUE) {
    /* loop finished, set the max tries variable to zero again... */
    src->wait_to_transfer = 0;

    while (src->wait_to_transfer++ < GMYTHTV_TRANSFER_MAX_WAITS &&
        (gmyth_livetv_is_recording (src->spawn_livetv) == FALSE))
      g_usleep (500);

    /* IS_RECORDING again, just like the MythTV backend does... */
    gmyth_livetv_is_recording (src->spawn_livetv);

    sleep (9);

  }

  if (ret == FALSE) {
#ifndef GST_DISABLE_GST_DEBUG
    if (src->mythtv_msgs_dbg)
      GST_INFO_OBJECT (src,
          "MythTV FileTransfer request failed when setting up socket connection!");
#endif
    goto begin_req_failed;
  }

  GST_INFO_OBJECT (src,
      "MythTV FileTransfer filesize = %lld, content_size = %lld!",
      src->file_transfer->filesize, src->content_size);

  src->content_size = src->file_transfer->filesize;

  src->do_start = FALSE;

  /* this is used for the buffer cache */
  src->bytes_queue = g_byte_array_sized_new (INTERNAL_BUFFER_SIZE);
  src->buffer_remain = 0;

  gst_pad_push_event (GST_BASE_SRC_PAD (GST_BASE_SRC (src)),
      gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_TIME, 0,
          src->content_size, 0));

done:

  if (chain_id_local != NULL) {
    g_string_free (chain_id_local, TRUE);
    chain_id_local = NULL;
  }

  return TRUE;

  /* ERRORS */
init_failed:
  {
    if (src->spawn_livetv != NULL)
      g_object_unref (src->spawn_livetv);

    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        (NULL), ("Could not initialize MythTV library (%i, %s)", ret,
            src->uri_name));
    return FALSE;
  }
begin_req_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        (NULL), ("Could not begin request sent to MythTV server (%i, %s)", ret,
            src->uri_name));
    return FALSE;
  }
change_progchain:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        (NULL), ("Seek failed, go to the next program info... (%s)",
            src->uri_name));

    gst_pad_push_event (GST_BASE_SRC_PAD (GST_BASE_SRC (src)),
        gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_TIME, 0, -1, 0));

    /* go to the next program chain */
    src->unique_setup = FALSE;
    src->update_prog_chain = TRUE;

    gst_mythtv_src_next_program_chain (src);

    return TRUE;
  }
}

/* create a new socket for connecting to the next program chain */
static gboolean
gst_mythtv_src_next_program_chain (GstMythtvSrc * src)
{
  GString *chain_id_local = NULL;

  gboolean ret = TRUE;

  if (!src->live_tv)
    goto init_failed;

  if (src->unique_setup == FALSE) {
    src->unique_setup = TRUE;
  } else {
    goto done;
  }

  GST_PAD_STREAM_LOCK (GST_BASE_SRC_PAD (GST_BASE_SRC (src)));

  if (src->file_transfer) {
    g_object_unref (src->file_transfer);
    src->file_transfer = NULL;
  }

  if (src->uri_name) {
    g_free (src->uri_name);
  }

  if (src->backend_info == NULL)
    src->backend_info = gmyth_backend_info_new_with_uri (src->uri_name);

  if (src->live_tv) {
    if (gmyth_livetv_next_program_chain (src->spawn_livetv) == FALSE) {
      GST_INFO_OBJECT (src, "[%s]Failed to go to the next program chain!!!",
          __FUNCTION__);
      ret = FALSE;
      goto init_failed;
    }
    /* set up the uri variable */
    src->uri_name = g_strdup (src->spawn_livetv->proginfo->pathname->str);
    chain_id_local = gmyth_tvchain_get_id (src->spawn_livetv->tvchain);
    if (chain_id_local != NULL) {
      src->live_chain_id = g_strdup (chain_id_local->str);
      GST_DEBUG_OBJECT (src, "[%s] Local chain ID = %s.", __FUNCTION__,
          src->live_chain_id);
    }
    src->live_tv_id = src->spawn_livetv->recorder->recorder_num;
    GST_LOG_OBJECT (src, "[%s] LiveTV id = %d, URI path = %s.", __FUNCTION__,
        src->live_tv_id, src->uri_name);
  }

  src->file_transfer = gmyth_file_transfer_new (src->backend_info);

  if (src->file_transfer == NULL) {
    goto init_failed;
  }

  ret = gmyth_file_transfer_open (src->file_transfer, src->uri_name);

  /* sets the Playback monitor connection */

  if (src->live_tv == TRUE && ret == TRUE) {
    /* loop finished, set the max tries variable to zero again... */
    src->wait_to_transfer = 0;

    g_usleep (200);

    while (src->wait_to_transfer++ < GMYTHTV_TRANSFER_MAX_WAITS &&
        (gmyth_livetv_is_recording (src->spawn_livetv) == FALSE))
      g_usleep (1000);
  }

  /* sets the FileTransfer instance connection (video/audio download) */

  if (ret == FALSE) {
#ifndef GST_DISABLE_GST_DEBUG
    if (src->mythtv_msgs_dbg)
      GST_ERROR_OBJECT (src,
          "MythTV FileTransfer request failed when setting up socket connection!");
#endif
    goto begin_req_failed;
  }
  src->content_size_last = src->content_size;

  src->content_size = src->file_transfer->filesize;
  if (src->live_tv) {
    src->wait_to_transfer = 0;
    while (src->wait_to_transfer++ < GMYTHTV_TRANSFER_MAX_WAITS &&
        src->content_size < GMYTHTV_TRANSFER_MAX_BUFFER)
      src->content_size = gst_mythtv_src_get_position (src);
  }

  src->read_offset = 0;

  if (src->bytes_queue != NULL) {
    g_byte_array_free (src->bytes_queue, TRUE);
  }

  src->bytes_queue = g_byte_array_sized_new (INTERNAL_BUFFER_SIZE);

done:
  src->update_prog_chain = FALSE;

  GST_PAD_STREAM_UNLOCK (GST_BASE_SRC_PAD (GST_BASE_SRC (src)));

  return TRUE;

  /* ERRORS */
init_failed:
  {
    if (src->spawn_livetv != NULL)
      g_object_unref (src->spawn_livetv);

    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        (NULL), ("Could not initialize MythTV library (%i, %s)", ret,
            src->uri_name));
    return FALSE;
  }
begin_req_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        (NULL), ("Could not begin request sent to MythTV server (%i, %s)", ret,
            src->uri_name));
    return FALSE;
  }

}

static gboolean
gst_mythtv_src_get_size (GstBaseSrc * bsrc, guint64 * size)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (bsrc);
  gboolean ret = TRUE;

  GST_LOG_OBJECT (src, "[%s] Differs from previous content size: %d (max.: %d)",
      __FUNCTION__, abs (src->content_size - src->prev_content_size),
      GMYTHTV_TRANSFER_MAX_BUFFER);

  if (src->live_tv) {
    ret = FALSE;
  } else if (src->live_tv && src->enable_timing_position
      && (abs (src->content_size - src->bytes_read) <
          GMYTHTV_TRANSFER_MAX_BUFFER)) {

    gint64 new_offset =
        gmyth_recorder_get_file_position (src->spawn_livetv->recorder);
    if (new_offset > 0 && new_offset > src->content_size) {
      src->content_size = new_offset;
    } else if (new_offset < src->content_size) {
      src->update_prog_chain = TRUE;
    }

  }

  *size = src->content_size;
  GST_LOG_OBJECT (src, "[%s] Content size = %lld", __FUNCTION__,
      src->content_size);

  return ret;

}

/* close the socket and associated resources
 * used both to recover from errors and go to NULL state */
static gboolean
gst_mythtv_src_stop (GstBaseSrc * bsrc)
{
  GstMythtvSrc *src;

  src = GST_MYTHTV_SRC (bsrc);

  if (src->uri_name) {
    g_free (src->uri_name);
    src->uri_name = NULL;
  }

  if (src->mythtv_caps) {
    gst_caps_unref (src->mythtv_caps);
    src->mythtv_caps = NULL;
  }

  src->eos = FALSE;

  return TRUE;
}

static gboolean
gst_mythtv_src_handle_event (GstPad * pad, GstEvent * event)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (GST_PAD_PARENT (pad));
  gint64 cont_size = 0;
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_WARNING_OBJECT (src, "[%s] Got EOS event!!!", __FUNCTION__);

      if (src->live_tv) {
        cont_size = gst_mythtv_src_get_position (src);
        if (cont_size > src->content_size) {
          src->content_size = cont_size;
          src->eos = FALSE;
        } else {
          src->eos = TRUE;
          gst_element_set_state (GST_ELEMENT (src), GST_STATE_NULL);
          gst_element_set_locked_state (GST_ELEMENT (src), FALSE);
        }
      }
      break;
    default:
      ret = gst_pad_event_default (pad, event);
  }

  return ret;
}

static gboolean
gst_mythtv_src_is_seekable (GstBaseSrc * push_src)
{
  return TRUE;
}

static gboolean
gst_mythtv_src_handle_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstMythtvSrc *myth = GST_MYTHTV_SRC (gst_pad_get_parent (pad));
  GstFormat formt;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 pos = -1;

      gst_query_parse_position (query, &formt, &pos);
      res = TRUE;
      if (formt == GST_FORMAT_BYTES) {
        gst_query_set_position (query, formt, pos = myth->read_offset);
        GST_DEBUG_OBJECT (myth, "POS %lld (BYTES).", pos);
      } else if (formt == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (myth, "POS %lld (TIME).", pos);
        res = gst_pad_query_default (pad, query);
      }
      break;
    }
    case GST_QUERY_DURATION:
    {
#if 0
      if (myth->duration != 0) {
        gint64 total;
        gint64 fps;

        fps = nuv->h->i_fpsn / nuv->h->i_fpsd;
        total =
            gst_util_uint64_scale_int (GST_SECOND, nuv->h->i_video_blocks, fps);
      }
#endif
      gint64 dur = -1;

      gst_query_parse_duration (query, &formt, &dur);
      if (formt == GST_FORMAT_BYTES) {
        gst_query_set_duration (query, formt, dur = myth->content_size);
        GST_DEBUG_OBJECT (myth, "DURATION %lld (BYTES).", dur);
      } else if (formt == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (myth, "DURATION %lld (TIME).", dur);
        gst_query_ref (query);
        res = gst_pad_query_default (pad, query);
      }
      res = TRUE;
      break;
    }
    default:
    {
      res = gst_pad_query_default (pad, query);
      break;
    }
  }

  gst_object_unref (myth);

  return res;
}

static GstStateChangeReturn
gst_mythtv_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstMythtvSrc *src = GST_MYTHTV_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_INFO_OBJECT (src, "[%s] READY to PAUSED called!", __FUNCTION__);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_INFO_OBJECT (src, "[%s] PAUSED to PLAYING called!", __FUNCTION__);
      if (src->live_tv) {
        if (!gmyth_recorder_send_frontend_ready_command (src->spawn_livetv->
                recorder))
          GST_WARNING_OBJECT (src,
              "[%s] Couldn't send the FRONTEND_READY message to the backend!",
              __FUNCTION__);
        else
          GST_DEBUG_OBJECT (src,
              "[%s] Message FRONTEND_READY was sent to the backend!",
              __FUNCTION__);
      }

      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_INFO_OBJECT (src, "[%s] READY to NULL called!", __FUNCTION__);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_INFO_OBJECT (src, "[%s] PLAYING to PAUSED called!", __FUNCTION__);
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_INFO_OBJECT (src, "[%s] PAUSED to READY called!", __FUNCTION__);
      if (src->live_tv) {
        if (!gmyth_recorder_send_frontend_ready_command (src->spawn_livetv->
                recorder))
          GST_WARNING_OBJECT (src,
              "[%s] Couldn't send the FRONTEND_READY message to the backend!",
              __FUNCTION__);
        else
          GST_DEBUG_OBJECT (src,
              "[%s] Message FRONTEND_READY was sent to the backend!",
              __FUNCTION__);
      }

      break;
    default:
      break;
  }

  return ret;
}

static void
gst_mythtv_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMythtvSrc *mythtvsrc = GST_MYTHTV_SRC (object);

  GST_OBJECT_LOCK (mythtvsrc);
  switch (prop_id) {
    case PROP_LOCATION:
    {
      if (!g_value_get_string (value)) {
        GST_WARNING ("location property cannot be NULL");
        goto done;
      }

      if (mythtvsrc->uri_name != NULL) {
        g_free (mythtvsrc->uri_name);
        mythtvsrc->uri_name = NULL;
      }
      mythtvsrc->uri_name = g_value_dup_string (value);

      break;
    }
#ifndef GST_DISABLE_GST_DEBUG
    case PROP_GMYTHTV_DBG:
    {
      mythtvsrc->mythtv_msgs_dbg = g_value_get_boolean (value);
      break;
    }
#endif
    case PROP_GMYTHTV_VERSION:
    {
      mythtvsrc->mythtv_version = g_value_get_int (value);
      break;
    }
    case PROP_GMYTHTV_LIVEID:
    {
      mythtvsrc->live_tv_id = g_value_get_int (value);
      break;
    }
    case PROP_GMYTHTV_LIVE:
    {
      mythtvsrc->live_tv = g_value_get_boolean (value);
      break;
    }
    case PROP_GMYTHTV_ENABLE_TIMING_POSITION:
    {
      mythtvsrc->enable_timing_position = g_value_get_boolean (value);
      break;
    }
    case PROP_GMYTHTV_LIVE_CHAINID:
    {
      if (!g_value_get_string (value)) {
        GST_WARNING ("MythTV Live chainid property cannot be NULL");
        goto done;
      }

      if (mythtvsrc->live_chain_id != NULL) {
        g_free (mythtvsrc->live_chain_id);
        mythtvsrc->live_chain_id = NULL;
      }
      mythtvsrc->live_chain_id = g_value_dup_string (value);
      break;
    }
    case PROP_GMYTHTV_CHANNEL_NUM:
    {
      mythtvsrc->channel_num = g_value_get_int (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (mythtvsrc);
done:
  return;
}

static void
gst_mythtv_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMythtvSrc *mythtvsrc = GST_MYTHTV_SRC (object);

  GST_OBJECT_LOCK (mythtvsrc);
  switch (prop_id) {
    case PROP_LOCATION:
    {
      gchar *str = g_strdup ("");

      if (mythtvsrc->uri_name == NULL) {
        g_free (mythtvsrc->uri_name);
        mythtvsrc->uri_name = NULL;
      } else {
        str = g_strdup (mythtvsrc->uri_name);
      }
      g_value_set_string (value, str);
      break;
    }
#ifndef GST_DISABLE_GST_DEBUG
    case PROP_GMYTHTV_DBG:
      g_value_set_boolean (value, mythtvsrc->mythtv_msgs_dbg);
      break;
#endif
    case PROP_GMYTHTV_VERSION:
    {
      g_value_set_int (value, mythtvsrc->mythtv_version);
      break;
    }
    case PROP_GMYTHTV_LIVEID:
    {
      g_value_set_int (value, mythtvsrc->live_tv_id);
      break;
    }
    case PROP_GMYTHTV_LIVE:
      g_value_set_boolean (value, mythtvsrc->live_tv);
      break;
    case PROP_GMYTHTV_ENABLE_TIMING_POSITION:
      g_value_set_boolean (value, mythtvsrc->enable_timing_position);
      break;
    case PROP_GMYTHTV_LIVE_CHAINID:
    {
      gchar *str = g_strdup ("");

      if (mythtvsrc->live_chain_id == NULL) {
        g_free (mythtvsrc->live_chain_id);
        mythtvsrc->live_chain_id = NULL;
      } else {
        str = g_strdup (mythtvsrc->live_chain_id);
      }
      g_value_set_string (value, str);
      break;
    }
    case PROP_GMYTHTV_CHANNEL_NUM:
    {
      g_value_set_int (value, mythtvsrc->channel_num);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (mythtvsrc);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "mythtvsrc", GST_RANK_NONE,
      GST_TYPE_MYTHTV_SRC);
}

/* this is the structure that gst-register looks for
 * so keep the name plugin_desc, or you cannot get your plug-in registered */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mythtv",
    "lib MythTV src",
    plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")


/*** GSTURIHANDLER INTERFACE *************************************************/
     static guint gst_mythtv_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_mythtv_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { "myth", "myths", NULL };

  return protocols;
}

static const gchar *
gst_mythtv_src_uri_get_uri (GstURIHandler * handler)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (handler);

  return src->uri_name;
}

static gboolean
gst_mythtv_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (handler);

  gchar *protocol;

  protocol = gst_uri_get_protocol (uri);
  if ((strcmp (protocol, "myth") != 0) && (strcmp (protocol, "myths") != 0)) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);
  g_object_set (src, "location", uri, NULL);

  return TRUE;
}

static void
gst_mythtv_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_mythtv_src_uri_get_type;
  iface->get_protocols = gst_mythtv_src_uri_get_protocols;
  iface->get_uri = gst_mythtv_src_uri_get_uri;
  iface->set_uri = gst_mythtv_src_uri_set_uri;
}

void
size_header_handler (void *userdata, const char *value)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (userdata);

  GST_DEBUG_OBJECT (src, "content size = %lld bytes", src->content_size);
}
