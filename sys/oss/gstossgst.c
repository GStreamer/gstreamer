/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstossgst.c: 
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


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include "gstossgst.h"

#include "gstosshelper.h"

static GstElementDetails gst_ossgst_details = {  
  "Audio Wrapper (OSS)",
  "Source/Audio",
  "Hijacks /dev/dsp to get the output of OSS apps into GStreamer",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
};

static void 			gst_ossgst_class_init		(GstOssGstClass *klass);
static void 			gst_ossgst_init		(GstOssGst *ossgst);

static GstElementStateReturn 	gst_ossgst_change_state	(GstElement *element);

static void 			gst_ossgst_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void 			gst_ossgst_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstBuffer* 		gst_ossgst_get 			(GstPad *pad);

/* OssGst signals and args */
enum {
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_MUTE,
  ARG_PROGRAM,
  /* FILL ME */
};

static GstPadTemplate*
ossgst_src_factory (void)
{
  return
    gst_pad_template_new (
  	"src",
  	GST_PAD_SRC,
  	GST_PAD_ALWAYS,
  	gst_caps_new (
  	  "ossgst_src",
    	  "audio/raw",
	  gst_props_new (
    	    "format",       GST_PROPS_STRING ("int"),
      	      "law",        GST_PROPS_INT (0),
      	      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      	      "signed",     GST_PROPS_LIST (
 		      	      GST_PROPS_BOOLEAN (FALSE),
 	      		      GST_PROPS_BOOLEAN (TRUE)
		      	    ),
      	      "width",      GST_PROPS_LIST (
   		      	      GST_PROPS_INT (8),
	      		      GST_PROPS_INT (16)
	      		    ),
      	      "depth",      GST_PROPS_LIST (
 	      		      GST_PROPS_INT (8),
	      		      GST_PROPS_INT (16)
	      		    ),
      	      "rate",       GST_PROPS_INT_RANGE (8000, 48000),
      	      "channels",   GST_PROPS_INT_RANGE (1, 2),
	      NULL)),
	NULL);
}


static GstElementClass *parent_class = NULL;
static GstPadTemplate *gst_ossgst_src_template;

static gchar *plugin_dir = NULL;

GType
gst_ossgst_get_type (void) 
{
  static GType ossgst_type = 0;

  if (!ossgst_type) {
    static const GTypeInfo ossgst_info = {
      sizeof(GstOssGstClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_ossgst_class_init,
      NULL,
      NULL,
      sizeof(GstOssGst),
      0,
      (GInstanceInitFunc)gst_ossgst_init,
    };
    ossgst_type = g_type_register_static (GST_TYPE_ELEMENT, "GstOssGst", &ossgst_info, 0);
  }

  return ossgst_type;
}

static void
gst_ossgst_class_init (GstOssGstClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MUTE,
    g_param_spec_boolean("mute","mute","mute",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PROGRAM,
    g_param_spec_string("command","command","command",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = gst_ossgst_set_property;
  gobject_class->get_property = gst_ossgst_get_property;

  gstelement_class->change_state = gst_ossgst_change_state;
}

static void 
gst_ossgst_init (GstOssGst *ossgst) 
{
  ossgst->srcpad = gst_pad_new_from_template (gst_ossgst_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (ossgst), ossgst->srcpad);

  gst_pad_set_get_function (ossgst->srcpad, gst_ossgst_get);

  ossgst->command = NULL;
}

static GstCaps* 
gst_ossgst_format_to_caps (gint format, gint stereo, gint rate) 
{
  GstCaps *caps = NULL;
  gint law = 0;
  gulong endianness = G_BYTE_ORDER;
  gboolean is_signed = TRUE;
  gint width = 16;
  gboolean supported = TRUE;

  GST_DEBUG (0, "have format 0x%08x %d %d", format, stereo, rate); 

  switch (format) {
    case AFMT_MU_LAW:
      law = 1;
      break;
    case AFMT_A_LAW:
      law = 2;
      break;
    case AFMT_U8:
      width = 8;
      is_signed = FALSE;
      break;
    case AFMT_S16_LE:
      width = 16;
      endianness = G_LITTLE_ENDIAN;
      is_signed = TRUE;
      break;
    case AFMT_S16_BE:
      endianness = G_BIG_ENDIAN;
      width = 16;
      is_signed = TRUE;
      break;
    case AFMT_S8:
      width = 8;
      is_signed = TRUE;
      break;
    case AFMT_U16_LE:
      width = 16;
      endianness = G_LITTLE_ENDIAN;
      is_signed = FALSE;
      break;
    case AFMT_U16_BE:
      width = 16;
      endianness = G_BIG_ENDIAN;
      is_signed = FALSE;
      break;
    case AFMT_IMA_ADPCM:
    case AFMT_MPEG:
#ifdef AFMT_AC3
    case AFMT_AC3:
#endif
    default:
      supported = FALSE;
      break;
  }

  if (supported) {
    caps = gst_caps_new (
		  "ossgst_caps",
		  "audio/raw",
		  gst_props_new (
			  "format",   		GST_PROPS_STRING ("int"),
			    "law",		GST_PROPS_INT (law),
			    "endianness",	GST_PROPS_INT (endianness),
			    "signed",		GST_PROPS_BOOLEAN (is_signed),
			    "width",		GST_PROPS_INT (width),
			    "depth",		GST_PROPS_INT (width),
			    "rate",		GST_PROPS_INT (rate),
			    "channels",		GST_PROPS_INT (stereo?2:1),
			    NULL));
  }
  else {
    g_warning ("gstossgst: program tried to use unsupported format %x\n", format);
  }

  return caps;
}

static GstBuffer* 
gst_ossgst_get (GstPad *pad) 
{
  GstOssGst *ossgst;
  GstBuffer *buf = NULL;
  command cmd;
  gboolean have_data = FALSE;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  /* this has to be an audio buffer */
  ossgst = GST_OSSGST (gst_pad_get_parent (pad));

  while (!have_data) {
    /* read the command */
    read (ossgst->fdout[0], &cmd, sizeof (command));

    switch (cmd.id) { 
      case CMD_DATA:
        buf = gst_buffer_new ();
        GST_BUFFER_SIZE (buf) = cmd.cmd.length;
        GST_BUFFER_DATA (buf) = g_malloc (GST_BUFFER_SIZE (buf));

        GST_BUFFER_SIZE (buf) = read (ossgst->fdout[0], GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
	have_data = TRUE;
        break;
      case CMD_FORMAT:
	{
	  GstCaps *caps;

	  caps = gst_ossgst_format_to_caps (cmd.cmd.format.format, 
					    cmd.cmd.format.stereo, 
					    cmd.cmd.format.rate); 

	  gst_pad_try_set_caps (ossgst->srcpad, caps);
	}
        break;
      default:
        break;
    }
  }

  return buf;
}

static void 
gst_ossgst_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstOssGst *ossgst;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_OSSGST (object));
  
  ossgst = GST_OSSGST (object);

  switch (prop_id) {
    case ARG_MUTE:
      ossgst->mute = g_value_get_boolean (value);
      break;
    case ARG_PROGRAM:
      if (ossgst->command)
	g_free (ossgst->command);
      ossgst->command = g_strdup (g_value_get_string (value));
      break;
    default:
      break;
  }
}

static void 
gst_ossgst_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  GstOssGst *ossgst;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_OSSGST (object));
  
  ossgst = GST_OSSGST (object);

  switch (prop_id) {
    case ARG_MUTE:
      g_value_set_boolean (value, ossgst->mute);
      break;
    case ARG_PROGRAM:
      g_value_set_string (value, ossgst->command);
      break;
    default:
      break;
  }
}

static gboolean 
gst_ossgst_spawn_process (GstOssGst *ossgst) 
{
  static gchar *ld_preload;

  pipe(ossgst->fdin);
  pipe(ossgst->fdout);

  GST_DEBUG (0, "about to fork");

  if((ossgst->childpid = fork()) == -1)
  {
    perror("fork");
    gst_element_error(GST_ELEMENT(ossgst),"forking");
    return FALSE;
  }
  GST_DEBUG (0,"forked %d", ossgst->childpid);

  if(ossgst->childpid == 0)
  {
    gchar **args;

    GST_DEBUG (0, "fork command %d", ossgst->childpid);

    ld_preload = getenv ("LD_PRELOAD");

    if (ld_preload == NULL) {
      ld_preload = "";
    }

    ld_preload = g_strconcat (ld_preload, " ", plugin_dir, G_DIR_SEPARATOR_S, 
		    "libgstosshelper.so", NULL);

    setenv ("LD_PRELOAD", ld_preload, TRUE);

    /* child */
    dup2(ossgst->fdin[0], HELPER_MAGIC_IN);  /* set the childs input stream */
    dup2(ossgst->fdout[1], HELPER_MAGIC_OUT);  /* set the childs output stream */
    
    /* split the arguments  */
    args = g_strsplit (ossgst->command, " ", 0);

    execvp(args[0], args);

    /* will only reach if error */
    perror("exec");
    gst_element_error(GST_ELEMENT(ossgst),"starting child process");
    return FALSE;

  }
  GST_FLAG_SET(ossgst,GST_OSSGST_OPEN);

  return TRUE;
}

static gboolean 
gst_ossgst_kill_process (GstOssGst *ossgst) 
{
  return TRUE;
}

static GstElementStateReturn 
gst_ossgst_change_state (GstElement *element) 
{
  g_return_val_if_fail (GST_IS_OSSGST (element), FALSE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_OSSGST_OPEN))
      gst_ossgst_kill_process (GST_OSSGST (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_OSSGST_OPEN)) {
      if (!gst_ossgst_spawn_process (GST_OSSGST (element))) {
        return GST_STATE_FAILURE;
      }
    }
  }
      
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  return GST_STATE_SUCCESS;
}

gboolean 
gst_ossgst_factory_init (GstPlugin *plugin) 
{ 
  GstElementFactory *factory;
  gchar **path;
  gint i =0;

  /* get the path of this plugin, we assume the helper progam lives in the */
  /* same directory. */
  path = g_strsplit (plugin->filename, G_DIR_SEPARATOR_S, 0);
  while (path[i]) {
    i++;
    if (path[i] == NULL) {
      g_free (path[i-1]);
      path[i-1] = NULL;
    }
  }
  plugin_dir = g_strjoinv (G_DIR_SEPARATOR_S, path);
  g_strfreev (path);

  factory = gst_element_factory_new ("ossgst", GST_TYPE_OSSGST, &gst_ossgst_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_ossgst_src_template = ossgst_src_factory ();
  gst_element_factory_add_pad_template (factory, gst_ossgst_src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

