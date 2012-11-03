/*
 * GStreamer MythTV Plug-in 
 * Copyright (C) <2006> Rosfran Borges <rosfran.borges@indt.org.br>
 * Copyright (C) <2007> Renato Filho <renato.filho@indt.org.br>  
 * This library is free software; you can
 * redistribute it and/or modify it under the terms of the GNU Library
 * General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later version.
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library 
 * General Public License for more details. You should have received a copy 
 * of the GNU Library General Public License along with this library; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin St,
 * Fifth Floor, Boston, MA 02110-1301, USA. 
 */

/**
 * SECTION:element-mythtvsrc
 * @see_also: nuvdemux
 *
 * MythTVSrc allows to access a remote MythTV backend streaming Video/Audio server,
 * and to render audio and video content through a TCP/IP connection to a specific
 * port on this server, and based on a known MythTV protocol that is based on 
 * some message passing, such as REQUEST_BLOCK on a specified number of bytes, to get
 * some chunk of remote file data.
 * You should pass the information aboute the remote MythTV backend server 
 * through the #GstMythtvSrc:location property.
 * 
 * <refsect2>
 * <title>Examples</title>
 * <para>
 * If you want to get the LiveTV content (set channel, TV tuner, RemoteEncoder, 
 * Recorder), use the following URI:
 * <programlisting>
 *  myth://xxx.xxx.xxx.xxx:6543/livetv?channel=BBC
 * </programlisting>
 *
 * This URI will configure the Recorder instance (used to change the channel,
 * start the TV multimedia content transmition, etc.), using
 * the IP address (xxx.xxx.xxx.xxx) and port number (6543) of the MythTV backend 
 * server, and setting the channel name to "BBC". 
 * 
 * To get a already recorded the MythTV NUV file, put the following URI:
 * <programlisting>
 *  myth://xxx.xxx.xxx.xxx:6543/filename.nuv
 * </programlisting>
 * 
 * Another possible way to use the LiveTV content, and just in the case you want to 
 * use the mysql database, put the location URI in the following format:
 * <programlisting>
 *  myth://mythtv:mythtv@xxx.xxx.xxx.xxx:6543/?mythconverg&channel=9
 * </programlisting>
 * 
 * Where the first field is the protocol (myth), the second and third are user 
 * name (mythtv) and password (mythtv), then backend host name and port number, 
 * and the last field is the database name (mythconverg).
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmythtvsrc.h"
#include <gmyth/gmyth_file.h>
#include <gmyth/gmyth_file_transfer.h>
#include <gmyth/gmyth_file_local.h>
#include <gmyth/gmyth_livetv.h>

#include <gmyth/gmyth_socket.h>
#include <gmyth/gmyth_tvchain.h>

#include <string.h>
#include <unistd.h>

GST_DEBUG_CATEGORY_STATIC (mythtvsrc_debug);
#define GST_GMYTHTV_ID_NUM                  1
#define GST_GMYTHTV_CHANNEL_DEFAULT_NUM     (-1)
#define GMYTHTV_VERSION_DEFAULT             30
#define GMYTHTV_TRANSFER_MAX_WAITS          100
#define GMYTHTV_TRANSFER_MAX_RESENDS        2
#define GMYTHTV_TRANSFER_MAX_BUFFER         (128*1024)
#define READ_SIZE                           (14*1024)
#define READ_SIZE_LIVETV                    (80*1024)
#define GST_FLOW_ERROR_NO_DATA              (-101)

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_GMYTHTV_VERSION,
  PROP_GMYTHTV_LIVE,
  PROP_GMYTHTV_LIVEID,
  PROP_GMYTHTV_LIVE_CHAINID,
  PROP_GMYTHTV_ENABLE_TIMING_POSITION,
  PROP_GMYTHTV_CHANNEL_NUM
};

static void gst_mythtv_src_clear (GstMythtvSrc * mythtv_src);

static void gst_mythtv_src_finalize (GObject * gobject);

static GstFlowReturn gst_mythtv_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);

static gboolean gst_mythtv_src_start (GstBaseSrc * bsrc);
static gboolean gst_mythtv_src_stop (GstBaseSrc * bsrc);
static gboolean gst_mythtv_src_get_size (GstBaseSrc * bsrc, guint64 * size);
static gboolean gst_mythtv_src_is_seekable (GstBaseSrc * push_src);

static GstFlowReturn gst_mythtv_src_do_seek (GstBaseSrc * base,
    GstSegment * segment);

static GstStateChangeReturn
gst_mythtv_src_change_state (GstElement * element, GstStateChange transition);

static void gst_mythtv_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_mythtv_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_mythtv_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

#if 0
static gboolean gst_mythtv_src_handle_query (GstPad * pad, GstQuery * query);
static gboolean gst_mythtv_src_handle_event (GstPad * pad, GstEvent * event);
#endif

static GMythFileReadResult do_read_request_response (GstMythtvSrc * src,
    guint size, GByteArray * data_ptr);

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

  gst_element_class_set_static_metadata (element_class, "MythTV client source",
      "Source/Network",
      "Control and receive data as a client over the network "
      "via raw socket connections using the MythTV protocol",
      "Rosfran Borges <rosfran.borges@indt.org.br>, "
      "Renato Filho <renato.filho@indt.org.br>");

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
          "\n\t\t\tmyth://a.com:23223/file.nuv"
          "\n\t\t\tmyth://a.com/?channel=123"
          "\n\t\t\tmyth://a.com/?channel=Channel%203"
          "\n\t\t\ta.com/file.nuv - default scheme 'myth'",
          "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_VERSION,
      g_param_spec_int ("mythtv-version", "mythtv-version",
          "Change MythTV version", 26, 30, 26,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_LIVEID,
      g_param_spec_int ("mythtv-live-id", "mythtv-live-id",
          "Change MythTV version",
          0, 200, GST_GMYTHTV_ID_NUM,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_LIVE_CHAINID,
      g_param_spec_string ("mythtv-live-chainid", "mythtv-live-chainid",
          "Sets the MythTV chain ID (from TV Chain)", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_LIVE,
      g_param_spec_boolean ("mythtv-live", "mythtv-live",
          "Enable MythTV Live TV content streaming", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_ENABLE_TIMING_POSITION,
      g_param_spec_boolean ("mythtv-enable-timing-position",
          "mythtv-enable-timing-position",
          "Enable MythTV Live TV content size continuous updating",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property
      (gobject_class, PROP_GMYTHTV_CHANNEL_NUM,
      g_param_spec_string ("mythtv-channel", "mythtv-channel",
          "Change MythTV channel number", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_mythtv_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_mythtv_src_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_mythtv_src_get_size);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_mythtv_src_is_seekable);
  gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_mythtv_src_do_seek);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_mythtv_src_create);

  GST_DEBUG_CATEGORY_INIT (mythtvsrc_debug, "mythtvsrc", 0,
      "MythTV Client Source");
}

static void
gst_mythtv_src_init (GstMythtvSrc * this, GstMythtvSrcClass * g_class)
{
  this->file = NULL;
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
  this->update_prog_chain = FALSE;
  this->channel_name = NULL;
  this->eos = FALSE;
  this->wait_to_transfer = 0;
  this->spawn_livetv = NULL;
  gst_base_src_set_format (GST_BASE_SRC (this), GST_FORMAT_BYTES);
#if 0
  gst_pad_set_event_function (GST_BASE_SRC_PAD (GST_BASE_SRC (this)),
      gst_mythtv_src_handle_event);
#endif
#if 0
  gst_pad_set_query_function (GST_BASE_SRC_PAD (GST_BASE_SRC (this)),
      gst_mythtv_src_handle_query);
#endif

}

static void
gst_mythtv_src_clear (GstMythtvSrc * mythtv_src)
{
  mythtv_src->unique_setup = FALSE;

#if 0
  if (mythtv_src->spawn_livetv) {
    g_object_unref (mythtv_src->spawn_livetv);
    mythtv_src->spawn_livetv = NULL;
  }

  if (mythtv_src->file) {
    g_object_unref (mythtv_src->file);
    mythtv_src->file = NULL;
  }

  if (mythtv_src->backend_info) {
    g_object_unref (mythtv_src->backend_info);
    mythtv_src->backend_info = NULL;
  }
#endif
}

static void
gst_mythtv_src_finalize (GObject * gobject)
{
  GstMythtvSrc *this = GST_MYTHTV_SRC (gobject);

  gst_mythtv_src_clear (this);

  if (this->uri_name) {
    g_free (this->uri_name);
    this->uri_name = NULL;
  }

  if (this->user_agent) {
    g_free (this->user_agent);
    this->user_agent = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static GMythFileReadResult
do_read_request_response (GstMythtvSrc * src, guint size, GByteArray * data_ptr)
{
  gint read = 0;
  guint sizetoread = size;
  gint max_iters = GMYTHTV_TRANSFER_MAX_RESENDS;
  GMythFileReadResult result;

  GST_LOG_OBJECT (src, "Starting: Reading %d bytes...", sizetoread);

  result = GMYTH_FILE_READ_OK;
  /*
   * Loop sending the Myth File Transfer request: Retry whilst
   * authentication fails and we supply it. 
   */

  while (sizetoread == size && --max_iters > 0) {
    /*
     * if ( gmyth_backend_info_is_local_file(src->backend_info) ) 
     */
    if (IS_GMYTH_FILE_LOCAL (src->file))
      result = gmyth_file_local_read (GMYTH_FILE_LOCAL (src->file),
          data_ptr, sizetoread, src->live_tv);
    else if (IS_GMYTH_FILE_TRANSFER (src->file))
      result = gmyth_file_transfer_read (GMYTH_FILE_TRANSFER (src->file),
          data_ptr, sizetoread, src->live_tv);

    if (data_ptr->len > 0) {
      read += data_ptr->len;
      sizetoread -= data_ptr->len;
    } else if (data_ptr->len <= 0) {
      if (src->live_tv == FALSE) {
        result = GMYTH_FILE_READ_EOF;
        goto eos;
      } else {
        if (result == GMYTH_FILE_READ_ERROR) {  /* -314 */
          GST_INFO_OBJECT (src, "[LiveTV] FileTransfer READ_ERROR!");
        }
        goto done;
      }
    }
    /*
     * else if (data_ptr->len == 0) goto done; 
     */
    if (read == sizetoread)
      goto done;
  }

  if ((read < 0 && !src->live_tv) || max_iters == 0) {
    result = GMYTH_FILE_READ_EOF;
    goto eos;
  }
  goto done;

eos:
  src->eos = TRUE;

done:
  GST_LOG_OBJECT (src, "Finished read: result %d", result);
  return result;
}

static GstFlowReturn
gst_mythtv_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstMythtvSrc *src;
  GstFlowReturn ret = GST_FLOW_OK;
  GByteArray *buffer;
  GMythFileReadResult result = GMYTH_FILE_READ_OK;

  src = GST_MYTHTV_SRC (psrc);

  buffer = g_byte_array_new ();
  if (src->live_tv)
    result = do_read_request_response (src, READ_SIZE_LIVETV, buffer);
  else
    result = do_read_request_response (src, READ_SIZE, buffer);

  if (result == GMYTH_FILE_READ_ERROR)
    goto read_error;

  *outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (*outbuf) = buffer->len;
  GST_BUFFER_MALLOCDATA (*outbuf) = buffer->data;
  GST_BUFFER_DATA (*outbuf) = GST_BUFFER_MALLOCDATA (*outbuf);
  GST_BUFFER_OFFSET (*outbuf) = src->read_offset;
  GST_BUFFER_OFFSET_END (*outbuf) =
      src->read_offset + GST_BUFFER_SIZE (*outbuf);

  src->read_offset += GST_BUFFER_SIZE (*outbuf);
  src->bytes_read += GST_BUFFER_SIZE (*outbuf);

  g_byte_array_free (buffer, FALSE);

  if (result == GMYTH_FILE_READ_NEXT_PROG_CHAIN) {
    GstPad *peer;

    peer = gst_pad_get_peer (GST_BASE_SRC_PAD (GST_BASE_SRC (psrc)));
    gst_pad_send_event (peer,
        gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES, 0, -1, 0));

    gst_object_unref (peer);
  }

  if (src->eos || (!src->live_tv && (src->bytes_read >= src->content_size)))
    ret = GST_FLOW_UNEXPECTED;

  GST_LOG_OBJECT (src, "Create finished: %d", ret);
  return ret;

read_error:
  GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
      ("Could not read any bytes (%d, %s)", result, src->uri_name));
  return GST_FLOW_ERROR;
}

static gboolean
gst_mythtv_src_do_seek (GstBaseSrc * base, GstSegment * segment)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (base);
  gint64 new_offset = -1;
  gint64 actual_seek = segment->start;
  gboolean ret = TRUE;

  GST_LOG_OBJECT (src, "seek, segment: %" GST_SEGMENT_FORMAT, segment);

  if (segment->format != GST_FORMAT_BYTES) {
    ret = FALSE;
    goto done;
  }
  GST_LOG_OBJECT (src, "actual_seek = %" G_GINT64_FORMAT ", read_offset = "
      "%" G_GINT64_FORMAT, actual_seek, src->read_offset);
  /*
   * verify if it needs to seek 
   */
  if (src->read_offset != actual_seek) {
    if (IS_GMYTH_FILE_LOCAL (src->file))
      new_offset = gmyth_file_local_seek (GMYTH_FILE_LOCAL (src->file),
          segment->start, G_SEEK_SET);
    else if (IS_GMYTH_FILE_TRANSFER (src->file))
      new_offset = gmyth_file_transfer_seek (GMYTH_FILE_TRANSFER (src->file),
          segment->start, G_SEEK_SET);
    if (G_UNLIKELY (new_offset < 0)) {
      ret = FALSE;
      if (!src->live_tv)
        goto eos;
    }

    src->read_offset = new_offset;

    if (ret == FALSE) {
      GST_INFO_OBJECT (src, "Failed to set the SEEK on segment!");
    }
  }

done:
  return ret;

eos:
  GST_DEBUG_OBJECT (src, "EOS found on seeking!!!");
  return FALSE;
}

/*
 * create a socket for connecting to remote server 
 */
static gboolean
gst_mythtv_src_start (GstBaseSrc * bsrc)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (bsrc);

  GString *chain_id_local = NULL;
  GMythURI *gmyth_uri = NULL;
  gboolean ret = TRUE;
  GstBaseSrc *basesrc;
  GstMessage *msg;

  if (src->unique_setup == FALSE) {
    src->unique_setup = TRUE;
  } else {
    goto done;
  }

  gmyth_uri = gmyth_uri_new_with_value (src->uri_name);
  src->backend_info = gmyth_backend_info_new_with_uri (src->uri_name);
  src->live_tv = gmyth_uri_is_livetv (gmyth_uri);

  if (src->live_tv) {
    gchar *ch = gmyth_uri_get_channel_name (gmyth_uri);
    src->spawn_livetv = gmyth_livetv_new (src->backend_info);

    if (ch != NULL)
      src->channel_name = ch;

    if (src->channel_name != NULL) {
      gboolean result;

      result = gmyth_livetv_channel_name_setup (src->spawn_livetv,
          src->channel_name);
      if (result == FALSE) {
        GST_INFO_OBJECT (src, "LiveTV setup felt down on error");
        ret = FALSE;
        goto init_failed;
      }
    } else {
      if (gmyth_livetv_setup (src->spawn_livetv) == FALSE) {
        GST_INFO_OBJECT (src, "LiveTV setup felt down on error");
        ret = FALSE;
        goto init_failed;
      }
    }

    src->file =
        GMYTH_FILE (gmyth_livetv_create_file_transfer (src->spawn_livetv));
    if (NULL == src->file) {
      GST_INFO_OBJECT (src, "[LiveTV] FileTransfer equals to NULL");
      ret = FALSE;
      goto init_failed;
    }

    /*
     * Check if the file is local to this specific client renderer 
     */
    if (gmyth_uri_is_local_file (gmyth_uri))
      ret = gmyth_file_local_open (GMYTH_FILE_LOCAL (src->file));
    else
      ret = gmyth_file_transfer_open (GMYTH_FILE_TRANSFER (src->file),
          (src->spawn_livetv->uri != NULL ?
              gmyth_uri_get_path (src->spawn_livetv->uri) :
              src->spawn_livetv->proginfo->pathname->str));
    /*
     * sets the mythtvsrc "location" property 
     */
    g_object_set (src, "location", gmyth_file_get_uri (src->file), NULL);

    if (!ret) {
      GST_INFO_OBJECT (src,
          "Error: couldn't open the FileTransfer from LiveTV source!");
      g_object_unref (src->file);
      src->file = NULL;
      goto init_failed;
    }
  } /* If live-tv */
  else {
    /*
     * Check if the file is local to this specific client renderer,
     * and tries to open a local connection 
     */
    if (gmyth_uri_is_local_file (gmyth_uri)) {
      src->file = GMYTH_FILE (gmyth_file_local_new (src->backend_info));
      ret = gmyth_file_local_open (GMYTH_FILE_LOCAL (src->file));
    } else {
      src->file = GMYTH_FILE (gmyth_file_transfer_new (src->backend_info));
      ret =
          gmyth_file_transfer_open (GMYTH_FILE_TRANSFER (src->file),
          src->uri_name);
    }
  }

  if (NULL == src->file) {
    GST_INFO_OBJECT (src, "FileTransfer is NULL");
    goto init_failed;
  }

  if (ret == FALSE) {
    GST_INFO_OBJECT (src,
        "MythTV FileTransfer request failed when setting up socket connection!");
    goto begin_req_failed;
  }

  GST_INFO_OBJECT (src, "MythTV FileTransfer filesize = %" G_GINT64_FORMAT ", "
      "content_size = %" G_GINT64_FORMAT, gmyth_file_get_filesize (src->file),
      src->content_size);

  src->content_size = gmyth_file_get_filesize (src->file);

  msg = gst_message_new_duration_changed (GST_OBJECT (src));
  gst_element_post_message (GST_ELEMENT (src), msg);

  src->do_start = FALSE;

  basesrc = GST_BASE_SRC_CAST (src);
  gst_segment_set_duration (&basesrc->segment, GST_FORMAT_BYTES,
      src->content_size);
  gst_element_post_message (GST_ELEMENT (src),
      gst_message_new_duration_changed (GST_OBJECT (src)));
#if 0
  gst_pad_push_event (GST_BASE_SRC_PAD (GST_BASE_SRC (src)),
      gst_event_new_new_segment (TRUE, 1.0,
          GST_FORMAT_BYTES, 0, src->content_size, 0));
#endif
done:
  if (gmyth_uri != NULL) {
    g_object_unref (gmyth_uri);
    gmyth_uri = NULL;
  }

  if (chain_id_local != NULL) {
    g_string_free (chain_id_local, TRUE);
    chain_id_local = NULL;
  }

  return TRUE;

  /*
   * ERRORS
   */
init_failed:
  if (gmyth_uri != NULL) {
    g_object_unref (gmyth_uri);
    gmyth_uri = NULL;
  }

  if (src->spawn_livetv != NULL) {
    g_object_unref (src->spawn_livetv);
    src->spawn_livetv = NULL;
  }

  GST_ELEMENT_ERROR (src, LIBRARY, INIT,
      (NULL),
      ("Could not initialize MythTV library (%i, %s)", ret, src->uri_name));


  gst_mythtv_src_clear (src);

  return FALSE;
begin_req_failed:
  if (gmyth_uri != NULL) {
    g_object_unref (gmyth_uri);
    gmyth_uri = NULL;
  }

  GST_ELEMENT_ERROR (src, LIBRARY, INIT,
      (NULL),
      ("Could not begin request sent to MythTV server (%i, %s)",
          ret, src->uri_name));
  return FALSE;
}

static gboolean
gst_mythtv_src_get_size (GstBaseSrc * bsrc, guint64 * size)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (bsrc);
  gboolean ret = TRUE;

  GST_LOG_OBJECT (src,
      "Differs from previous content size: %d (max.: %d)",
      abs (src->content_size - src->prev_content_size),
      GMYTHTV_TRANSFER_MAX_BUFFER);

  if (src->live_tv) {
    ret = FALSE;
  } else if (src->live_tv && src->enable_timing_position
      && (abs (src->content_size - src->bytes_read) <
          GMYTHTV_TRANSFER_MAX_BUFFER)) {
    gint64 new_offset;

    new_offset = gmyth_recorder_get_file_position (src->spawn_livetv->recorder);
    if (new_offset > 0 && new_offset > src->content_size) {
      src->content_size = new_offset;
    } else if (new_offset < src->content_size) {
      src->update_prog_chain = TRUE;
    }
  }

  *size = src->content_size;
  GST_LOG_OBJECT (src, "Content size = %" G_GINT64_FORMAT, src->content_size);
  return ret;
}

/*
 * close the socket and associated resources used both to recover from
 * errors and go to NULL state 
 */
static gboolean
gst_mythtv_src_stop (GstBaseSrc * bsrc)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (bsrc);

  gst_mythtv_src_clear (src);
  return TRUE;
}

#if 0
static gint64
gst_mythtv_src_get_position (GstMythtvSrc * src)
{
  gint64 size_tmp = 0;
  guint max_tries = 2;

  if (src->live_tv == TRUE &&
      (abs (src->content_size - src->bytes_read) <
          GMYTHTV_TRANSFER_MAX_BUFFER)) {

  get_file_pos:
    g_usleep (10);
    size_tmp = gmyth_recorder_get_file_position (src->spawn_livetv->recorder);
    if (size_tmp > (src->content_size + GMYTHTV_TRANSFER_MAX_BUFFER))
      src->content_size = size_tmp;
    else if (size_tmp > 0 && --max_tries > 0)
      goto get_file_pos;
    GST_LOG_OBJECT (src, "file_position = %" G_GINT64_FORMAT, size_tmp);
    /*
     * sets the last content size amount before it can be updated 
     */
    src->prev_content_size = src->content_size;
  }
  return src->content_size;
}

static gboolean
gst_mythtv_src_handle_event (GstPad * pad, GstEvent * event)
{
  GstMythtvSrc *src = GST_MYTHTV_SRC (GST_PAD_PARENT (pad));
  gint64 cont_size = 0;
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
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
  GST_DEBUG_OBJECT (src, "HANDLE EVENT %d", ret);
  return ret;
}
#endif
static gboolean
gst_mythtv_src_is_seekable (GstBaseSrc * push_src)
{
  return TRUE;
}

#if 0
static gboolean
gst_mythtv_src_handle_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstMythtvSrc *myth = GST_MYTHTV_SRC (gst_pad_get_parent (pad));
  GstFormat formt;


  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &formt, NULL);
      if (formt == GST_FORMAT_BYTES) {
        gst_query_set_position (query, formt, myth->read_offset);
        GST_DEBUG_OBJECT (myth, "POS %" G_GINT64_FORMAT, myth->read_offset);
        res = TRUE;
      } else if (formt == GST_FORMAT_TIME) {
        res = gst_pad_query_default (pad, query);
      }
      break;
    case GST_QUERY_DURATION:
      gst_query_parse_duration (query, &formt, NULL);
      if (formt == GST_FORMAT_BYTES) {
        gint64 size = myth->content_size;

        gst_query_set_duration (query, GST_FORMAT_BYTES, 10);
        GST_DEBUG_OBJECT (myth, "SIZE %" G_GINT64_FORMAT, size);
        res = TRUE;
      } else if (formt == GST_FORMAT_TIME) {
        res = gst_pad_query_default (pad, query);
      }
      break;
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (myth);

  return res;
}
#endif
static GstStateChangeReturn
gst_mythtv_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstMythtvSrc *src = GST_MYTHTV_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!src->uri_name) {
        GST_WARNING_OBJECT (src, "Invalid location");
        return ret;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (src->live_tv) {
        if (!gmyth_recorder_send_frontend_ready_command
            (src->spawn_livetv->recorder))
          GST_WARNING_OBJECT (src,
              "Couldn't send the FRONTEND_READY message to the backend!");
        else
          GST_DEBUG_OBJECT (src, "FRONTEND_READY was sent to the backend");
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_mythtv_src_clear (src);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
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
      if (!g_value_get_string (value)) {
        GST_WARNING ("location property cannot be NULL");
        break;
      }

      if (mythtvsrc->uri_name != NULL) {
        g_free (mythtvsrc->uri_name);
        mythtvsrc->uri_name = NULL;
      }
      mythtvsrc->uri_name = g_value_dup_string (value);
      break;
    case PROP_GMYTHTV_VERSION:
      mythtvsrc->mythtv_version = g_value_get_int (value);
      break;
    case PROP_GMYTHTV_LIVEID:
      mythtvsrc->live_tv_id = g_value_get_int (value);
      break;
    case PROP_GMYTHTV_LIVE:
      mythtvsrc->live_tv = g_value_get_boolean (value);
      break;
    case PROP_GMYTHTV_ENABLE_TIMING_POSITION:
      mythtvsrc->enable_timing_position = g_value_get_boolean (value);
      break;
    case PROP_GMYTHTV_LIVE_CHAINID:
      if (!g_value_get_string (value)) {
        GST_WARNING_OBJECT (object,
            "MythTV Live chainid property cannot be NULL");
        break;
      }

      if (mythtvsrc->live_chain_id != NULL) {
        g_free (mythtvsrc->live_chain_id);
        mythtvsrc->live_chain_id = NULL;
      }
      mythtvsrc->live_chain_id = g_value_dup_string (value);
      break;
    case PROP_GMYTHTV_CHANNEL_NUM:
      mythtvsrc->channel_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (mythtvsrc);
}

static void
gst_mythtv_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMythtvSrc *mythtvsrc = GST_MYTHTV_SRC (object);

  GST_OBJECT_LOCK (mythtvsrc);
  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, mythtvsrc->uri_name);
      break;
    case PROP_GMYTHTV_VERSION:
      g_value_set_int (value, mythtvsrc->mythtv_version);
      break;
    case PROP_GMYTHTV_LIVEID:
      g_value_set_int (value, mythtvsrc->live_tv_id);
      break;
    case PROP_GMYTHTV_LIVE:
      g_value_set_boolean (value, mythtvsrc->live_tv);
      break;
    case PROP_GMYTHTV_ENABLE_TIMING_POSITION:
      g_value_set_boolean (value, mythtvsrc->enable_timing_position);
      break;
    case PROP_GMYTHTV_LIVE_CHAINID:
      g_value_set_string (value, mythtvsrc->live_chain_id);
      break;
    case PROP_GMYTHTV_CHANNEL_NUM:
      g_value_set_string (value, mythtvsrc->channel_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (mythtvsrc);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "mythtvsrc", GST_RANK_NONE,
      GST_TYPE_MYTHTV_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mythtv,
    "lib MythTV src",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);


/*** GSTURIHANDLER INTERFACE *************************************************/
static guint
gst_mythtv_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_mythtv_src_uri_get_protocols (void)
{
  static const gchar *protocols[] = { "myth", "myths", NULL };

  return (gchar **) protocols;
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
  if ((strcmp (protocol, "myth") != 0)
      && (strcmp (protocol, "myths") != 0)) {
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
