/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
/*************************************************************************/
/*                                                                       */
/*                Centre for Speech Technology Research                  */
/*                     University of Edinburgh, UK                       */
/*                        Copyright (c) 1999                             */
/*                        All Rights Reserved.                           */
/*                                                                       */
/*  Permission is hereby granted, free of charge, to use and distribute  */
/*  this software and its documentation without restriction, including   */
/*  without limitation the rights to use, copy, modify, merge, publish,  */
/*  distribute, sublicense, and/or sell copies of this work, and to      */
/*  permit persons to whom this work is furnished to do so, subject to   */
/*  the following conditions:                                            */
/*   1. The code must retain the above copyright notice, this list of    */
/*      conditions and the following disclaimer.                         */
/*   2. Any modifications must be clearly marked as such.                */
/*   3. Original authors' names are not deleted.                         */
/*   4. The authors' names are not used to endorse or promote products   */
/*      derived from this software without specific prior written        */
/*      permission.                                                      */
/*                                                                       */
/*  THE UNIVERSITY OF EDINBURGH AND THE CONTRIBUTORS TO THIS WORK        */
/*  DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING      */
/*  ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT   */
/*  SHALL THE UNIVERSITY OF EDINBURGH NOR THE CONTRIBUTORS BE LIABLE     */
/*  FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES    */
/*  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN   */
/*  AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,          */
/*  ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF       */
/*  THIS SOFTWARE.                                                       */
/*                                                                       */
/*************************************************************************/
/*             Author :  Alan W Black (awb@cstr.ed.ac.uk)                */
/*             Date   :  March 1999                                      */
/*-----------------------------------------------------------------------*/
/*                                                                       */
/* Client end of Festival server API in C designed specifically for      */
/* Galaxy Communicator use though might be of use for other things       */
/*                                                                       */
/* This is a modified version of the standalone client as provided in    */
/* festival example code: festival_client.c                              */
/*                                                                       */
/*=======================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gstfestival.h"

static void		gst_festival_class_init		(GstFestivalClass *klass);
static void		gst_festival_init		(GstFestival *festival);

static GstCaps*		text_type_find			(GstBuffer *buf, gpointer private);

static void		gst_festival_chain		(GstPad *pad, GstBuffer *buf);
static GstElementStateReturn 
			gst_festival_change_state 	(GstElement *element);

static FT_Info* 	festival_default_info		(void);
static char*		socket_receive_file_to_buff	(int fd,int *size);
static char*		client_accept_s_expr		(int fd);

/* elementfactory information */
static GstElementDetails gst_festival_details = {
  "Festival synthesizer",
  "Filter/Audio",
  "Synthesizes plain text into audio",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
};

GST_PAD_TEMPLATE_FACTORY (sink_template_factory,
  "festival_sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "festival_wav",   
    "text/plain",  
    NULL
  )
)

GST_PAD_TEMPLATE_FACTORY (src_template_factory,
  "festival_src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "festival_raw",   
    "audio/raw",  
      "format",            GST_PROPS_STRING ("int"),
       "law",              GST_PROPS_INT (0),
       "endianness",       GST_PROPS_INT (G_BYTE_ORDER),
       "signed",           GST_PROPS_BOOLEAN (TRUE),
       "width",            GST_PROPS_LIST (
	                     GST_PROPS_INT (8),
	                     GST_PROPS_INT (16)
			   ),
       "depth",            GST_PROPS_LIST (
	                     GST_PROPS_INT (8),
	                     GST_PROPS_INT (16)
			   ),
       "rate",             GST_PROPS_INT_RANGE (8000, 48000), 
       "channels",         GST_PROPS_INT_RANGE (1, 2)
  )
)

/* typefactory for 'wav' */
static GstTypeDefinition 
textdefinition = 
{
  "festival_text/plain",
  "text/plain",
  ".txt",
  text_type_find,
};


/* Festival signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static GstElementClass *parent_class = NULL;
/*static guint gst_festival_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_festival_get_type (void) 
{
  static GType festival_type = 0;

  if (!festival_type) {
    static const GTypeInfo festival_info = {
      sizeof(GstFestivalClass),      
      NULL,
      NULL,
      (GClassInitFunc) gst_festival_class_init,
      NULL,
      NULL,
      sizeof(GstFestival),
      0,
      (GInstanceInitFunc) gst_festival_init,
    };
    festival_type = g_type_register_static (GST_TYPE_ELEMENT, "GstFestival", &festival_info, 0);
  }
  return festival_type;
}

static void
gst_festival_class_init (GstFestivalClass *klass) 
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_festival_change_state;
}

static void 
gst_festival_init (GstFestival *festival) 
{
  festival->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_template_factory), "sink");
  gst_pad_set_chain_function (festival->sinkpad, gst_festival_chain);
  gst_element_add_pad (GST_ELEMENT (festival), festival->sinkpad);

  festival->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (festival), festival->srcpad);

  festival->info = festival_default_info();
}

static GstCaps*
text_type_find (GstBuffer *buf, gpointer private)
{
  gchar *data = GST_BUFFER_DATA (buf);
  gint i;

  for (i=0; i<GST_BUFFER_SIZE (buf); i++) {
    if (!isascii(*(data+i)))
      return NULL;
  }

  return gst_caps_new ("text_type_find", "text/plain", NULL);
}


static void
gst_festival_chain (GstPad *pad, GstBuffer *buf)
{
  gchar *wavefile;
  int filesize;
  FILE *fd;
  char *p;
  char ack[4];
  int n;
  GstFestival *festival;
  GstBuffer *outbuf;
  glong size;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  g_return_if_fail (GST_BUFFER_DATA (buf) != NULL);

  festival = GST_FESTIVAL (gst_pad_get_parent (pad));
  GST_DEBUG (0, "gst_festival_chain: got buffer in '%s'",
          gst_object_get_name (GST_OBJECT (festival)));

  fd = fdopen(dup(festival->info->server_fd),"wb");
    
  size = GST_BUFFER_SIZE (buf);

  /* Copy text over to server, escaping any quotes */
  fprintf(fd,"(tts_textall \"\n");
  for (p=GST_BUFFER_DATA(buf); p && (*p != '\0') && size; p++, size--)
  {
    if ((*p == '"') || (*p == '\\'))
      putc('\\',fd);
    putc(*p,fd);
  }
  fprintf(fd,"\" \"%s\")\n",festival->info->text_mode);
  fclose(fd);

  /* Read back info from server */
  /* This assumes only one waveform will come back, also LP is unlikely */
  wavefile = NULL;
  do {
    for (n=0; n < 3; )
      n += read(festival->info->server_fd,ack+n,3-n);
    ack[3] = '\0';
    if (strcmp(ack,"WV\n") == 0)         /* receive a waveform */
      wavefile = socket_receive_file_to_buff (festival->info->server_fd, &filesize);
    else if (strcmp(ack,"LP\n") == 0)    /* receive an s-expr */
      client_accept_s_expr(festival->info->server_fd);
    else if (strcmp(ack,"ER\n") == 0)    /* server got an error */
    {
      fprintf(stderr,"festival_client: server returned error\n");
       break;
    }

    if (wavefile) {
      outbuf = gst_buffer_new ();
      GST_BUFFER_DATA (outbuf) = wavefile;
      GST_BUFFER_SIZE (outbuf) = filesize;
      
      if (!GST_PAD_CAPS (festival->srcpad)) {
	gst_pad_try_set_caps (festival->srcpad,
			GST_CAPS_NEW (
				"festival_src",
				"audio/raw",
				  "format",	GST_PROPS_STRING ("int"),
       				  "law",	GST_PROPS_INT (0),
       				  "endianness", GST_PROPS_INT (G_BYTE_ORDER),
       				  "signed",     GST_PROPS_BOOLEAN (TRUE),
       				  "width",      GST_PROPS_INT (16),
       				  "depth",      GST_PROPS_INT (16),
       				  "rate",      	GST_PROPS_INT (16000),
       				  "channels",  	GST_PROPS_INT (1)
				  ));
      }
      gst_pad_push (festival->srcpad, outbuf);

      wavefile = NULL;
    }
  } while (strcmp(ack,"OK\n") != 0);
    
  gst_buffer_unref (buf);
}

static FT_Info*
festival_default_info (void)
{
  FT_Info *info;
  info = (FT_Info *)malloc(1 * sizeof(FT_Info));
    
  info->server_host = FESTIVAL_DEFAULT_SERVER_HOST;
  info->server_port = FESTIVAL_DEFAULT_SERVER_PORT;
  info->text_mode = FESTIVAL_DEFAULT_TEXT_MODE;

  info->server_fd = -1;
    
  return info;
}

static int
festival_socket_open (const char *host, int port)
{   
    /* Return an FD to a remote server */
  struct sockaddr_in serv_addr;
  struct hostent *serverhost;
  int fd;

  fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (fd < 0)  
  {
    fprintf(stderr,"festival_client: can't get socket\n");
    return -1;
  }
  memset(&serv_addr, 0, sizeof(serv_addr));
  if ((serv_addr.sin_addr.s_addr = inet_addr(host)) == -1)
  {
    /* its a name rather than an ipnum */
    serverhost = gethostbyname(host);
    if (serverhost == (struct hostent *)0)
    {
      fprintf(stderr,"festival_client: gethostbyname failed\n");
      return -1;
    }
    memmove(&serv_addr.sin_addr,serverhost->h_addr, serverhost->h_length);
  }
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);

  if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
  {
    fprintf(stderr,"festival_client: connect to server failed\n");
    return -1;
  }

  return fd;
}


static char*
client_accept_s_expr (int fd)
{
  /* Read s-expression from server, as a char * */
  char *expr;
  int filesize;

  expr = socket_receive_file_to_buff(fd,&filesize);
  expr[filesize] = '\0';

  return expr;
}

static char*
socket_receive_file_to_buff (int fd, int *size)
{
  /* Receive file (probably a waveform file) from socket using   */
  /* Festival key stuff technique, but long winded I know, sorry */
  /* but will receive any file without closeing the stream or    */
  /* using OOB data                                              */
  static const char *file_stuff_key = "ft_StUfF_key"; /* must == Festival's key */
  char *buff;
  int bufflen;
  int n,k,i;
  char c;

  bufflen = 1024;
  buff = (char *)malloc(bufflen);
  *size=0;

  for (k=0; file_stuff_key[k] != '\0';)
  {
    n = read(fd,&c,1);
    if (n==0) break;  /* hit stream eof before end of file */
    if ((*size)+k+1 >= bufflen) {
      /* +1 so you can add a NULL if you want */
      bufflen += bufflen/4;
      buff = (char *)realloc(buff,bufflen);
    }
    if (file_stuff_key[k] == c)
      k++;
    else if ((c == 'X') && (file_stuff_key[k+1] == '\0')) { 
      /* It looked like the key but wasn't */
      for (i=0; i < k; i++,(*size)++) 
    	buff[*size] = file_stuff_key[i];
      k=0;
      /* omit the stuffed 'X' */
    }
    else {
      for (i=0; i < k; i++,(*size)++)
    	buff[*size] = file_stuff_key[i];
      k=0;
      buff[*size] = c;
      (*size)++;
    }
  }

  return buff;
}

/***********************************************************************/
/* Public Functions to this API                                        */
/***********************************************************************/

static gboolean
gst_festival_open (GstFestival *festival)
{
  /* Open socket to server */
  if (festival->info == NULL)
    festival->info = festival_default_info();

  festival->info->server_fd = 
    festival_socket_open(festival->info->server_host, festival->info->server_port);
  if (festival->info->server_fd == -1)
    return FALSE;

  return TRUE;
}

static void
gst_festival_close (GstFestival *festival)
{
  if (festival->info == NULL)
    return;

  if (festival->info->server_fd != -1)
    close(festival->info->server_fd);

  return;
}

static GstElementStateReturn
gst_festival_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_FESTIVAL (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_FESTIVAL_OPEN))
      gst_festival_close (GST_FESTIVAL (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_FESTIVAL_OPEN)) {
      if (!gst_festival_open (GST_FESTIVAL (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  /* create an elementfactory for the festival element */
  factory = gst_element_factory_new ("festival", GST_TYPE_FESTIVAL,
                                    &gst_festival_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  /* register src pads */
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_template_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_template_factory));

  type = gst_type_factory_new (&textdefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "festival",
  plugin_init
};
