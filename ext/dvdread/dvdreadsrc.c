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

/**
 * Copyright (C) 2001 Billy Biggs <vektor@dumbterm.net>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <linux/cdrom.h>
#include <assert.h>

#include <dvdreadsrc.h>

#include "config.h"

#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_types.h>
#include <dvdread/ifo_read.h>
#include <dvdread/nav_read.h>
#include <dvdread/nav_print.h>

struct _DVDReadSrcPrivate {
  GstElement element;
  /* pads */
  GstPad *srcpad;

  /* location */
  gchar *location;

  gboolean new_seek;

  int title, chapter, angle;
  int pgc_id, start_cell, cur_cell, cur_pack;
  int ttn, pgn, next_cell;
  dvd_reader_t *dvd;
  dvd_file_t *dvd_title;
  ifo_handle_t *vmg_file;
  tt_srpt_t *tt_srpt;
  ifo_handle_t *vts_file;
  vts_ptt_srpt_t *vts_ptt_srpt;
  pgc_t *cur_pgc;
};


GstElementDetails dvdreadsrc_details = {
  "DVD Source",
  "Source/File/DVD",
  "Access a DVD title/chapter/angle using libdvdread",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 2001",
};


/* DVDReadSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
  ARG_TITLE,
  ARG_CHAPTER,
  ARG_ANGLE
};


static void 		dvdreadsrc_class_init	(DVDReadSrcClass *klass);
static void 		dvdreadsrc_init		(DVDReadSrc *dvdreadsrc);

static void 		dvdreadsrc_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void 		dvdreadsrc_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/*static GstBuffer *	dvdreadsrc_get		(GstPad *pad); */
static void     	dvdreadsrc_loop		(GstElement *element);
/*static GstBuffer *	dvdreadsrc_get_region	(GstPad *pad,gulong offset,gulong size); */

static GstElementStateReturn 	dvdreadsrc_change_state 	(GstElement *element);


static GstElementClass *parent_class = NULL;
/*static guint dvdreadsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
dvdreadsrc_get_type (void) 
{
  static GType dvdreadsrc_type = 0;

  if (!dvdreadsrc_type) {
    static const GTypeInfo dvdreadsrc_info = {
      sizeof(DVDReadSrcClass),      NULL,
      NULL,
      (GClassInitFunc)dvdreadsrc_class_init,
      NULL,
      NULL,
      sizeof(DVDReadSrc),
      0,
      (GInstanceInitFunc)dvdreadsrc_init,
    };
    dvdreadsrc_type = g_type_register_static (GST_TYPE_ELEMENT, "DVDReadSrc", &dvdreadsrc_info, 0);
  }
  return dvdreadsrc_type;
}

static void
dvdreadsrc_class_init (DVDReadSrcClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LOCATION,
    g_param_spec_string("location","location","location",
                        NULL, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TITLE,
    g_param_spec_int("title","title","title",
                     0,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHAPTER,
    g_param_spec_int("chapter","chapter","chapter",
                     0,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ANGLE,
    g_param_spec_int("angle","angle","angle",
                     0,G_MAXINT,0,G_PARAM_READWRITE));

  gobject_class->set_property = GST_DEBUG_FUNCPTR(dvdreadsrc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR(dvdreadsrc_get_property);

  gstelement_class->change_state = dvdreadsrc_change_state;
}

static void 
dvdreadsrc_init (DVDReadSrc *dvdreadsrc) 
{
  dvdreadsrc->priv = g_new(DVDReadSrcPrivate, 1);
  dvdreadsrc->priv->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (dvdreadsrc), dvdreadsrc->priv->srcpad);
  gst_element_set_loop_function (GST_ELEMENT(dvdreadsrc), GST_DEBUG_FUNCPTR(dvdreadsrc_loop));

  dvdreadsrc->priv->location = g_strdup("/dev/dvd");
  dvdreadsrc->priv->new_seek = FALSE;
  dvdreadsrc->priv->title = 1;
  dvdreadsrc->priv->chapter = 1;
  dvdreadsrc->priv->angle = 1;
}

/* FIXME: this code is not being used */
#ifdef PLEASEFIXTHISCODE
static void
dvdreadsrc_destroy (DVDReadSrc *dvdreadsrc)
{
  /* FIXME */
  g_print("FIXME\n");
  g_free(dvdreadsrc->priv);
}
#endif

static void 
dvdreadsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  DVDReadSrc *src;
  DVDReadSrcPrivate *priv;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DVDREADSRC (object));
  
  src = DVDREADSRC (object);
  priv = src->priv;

  switch (prop_id) {
    case ARG_LOCATION:
      /* the element must be stopped in order to do this */
      /*g_return_if_fail(!GST_FLAG_IS_SET(src,GST_STATE_RUNNING)); */

      if (priv->location)
        g_free (priv->location);
      /* clear the filename if we get a NULL (is that possible?) */
      if (g_value_get_string (value) == NULL)
        priv->location = g_strdup("/dev/dvd");
      /* otherwise set the new filename */
      else
        priv->location = g_strdup (g_value_get_string (value));  
      break;
    case ARG_TITLE:
      priv->title = g_value_get_int (value) - 1;
      priv->new_seek = TRUE;
      break;
    case ARG_CHAPTER:
      priv->chapter = g_value_get_int (value) - 1;
      priv->new_seek = TRUE;
      break;
    case ARG_ANGLE:
      priv->angle = g_value_get_int (value) - 1;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void 
dvdreadsrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  DVDReadSrc *src;
  DVDReadSrcPrivate *priv;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DVDREADSRC (object));
  
  src = DVDREADSRC (object);
  priv = src->priv;

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, priv->location);
      break;
    case ARG_TITLE:
      g_value_set_int (value, priv->title + 1);
      break;
    case ARG_CHAPTER:
      g_value_set_int (value, priv->chapter + 1);
      break;
    case ARG_ANGLE:
      g_value_set_int (value, priv->angle + 1);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * Returns true if the pack is a NAV pack.  This check is clearly insufficient,
 * and sometimes we incorrectly think that valid other packs are NAV packs.  I
 * need to make this stronger.
 */
static int
is_nav_pack( unsigned char *buffer )
{
    return ( buffer[ 41 ] == 0xbf && buffer[ 1027 ] == 0xbf );
}

static int
_open(DVDReadSrcPrivate *priv, const gchar *location)
{
  g_return_val_if_fail(priv != NULL, -1);
  g_return_val_if_fail(location != NULL, -1);

  /**
   * Open the disc.
   */
  priv->dvd = DVDOpen( location );
  if( !priv->dvd ) {
    fprintf( stderr, "Couldn't open DVD: %s\n", location );
    return -1;
  }


  /**
   * Load the video manager to find out the information about the titles on
   * this disc.
   */
  priv->vmg_file = ifoOpen( priv->dvd, 0 );
  if( !priv->vmg_file ) {
    fprintf( stderr, "Can't open VMG info.\n" );
    DVDClose( priv->dvd );
    return -1;
  }
  priv->tt_srpt = priv->vmg_file->tt_srpt;

  return 0;
}

static int
_close(DVDReadSrcPrivate *priv)
{
    ifoClose( priv->vts_file );
    ifoClose( priv->vmg_file );
    DVDCloseFile( priv->dvd_title );
    DVDClose( priv->dvd );
    return 0;
}

static int
_seek(DVDReadSrcPrivate *priv, int title, int chapter, int angle)
{
    /**
     * Make sure our title number is valid.
     */
    fprintf( stderr, "There are %d titles on this DVD.\n",
             priv->tt_srpt->nr_of_srpts );
    if( title < 0 || title >= priv->tt_srpt->nr_of_srpts ) {
        fprintf( stderr, "Invalid title %d.\n", title + 1 );
        ifoClose( priv->vmg_file );
        DVDClose( priv->dvd );
        return -1;
    }


    /**
     * Make sure the chapter number is valid for this title.
     */
    fprintf( stderr, "There are %d chapters in this title.\n",
             priv->tt_srpt->title[ title ].nr_of_ptts );

    if( chapter < 0 || chapter >= priv->tt_srpt->title[ title ].nr_of_ptts ) {
        fprintf( stderr, "Invalid chapter %d\n", chapter + 1 );
        ifoClose( priv->vmg_file );
        DVDClose( priv->dvd );
        return -1;
    }


    /**
     * Make sure the angle number is valid for this title.
     */
    fprintf( stderr, "There are %d angles in this title.\n",
             priv->tt_srpt->title[ title ].nr_of_angles );
    if( angle < 0 || angle >= priv->tt_srpt->title[ title ].nr_of_angles ) {
        fprintf( stderr, "Invalid angle %d\n", angle + 1 );
        ifoClose( priv->vmg_file );
        DVDClose( priv->dvd );
        return -1;
    }


    /**
     * Load the VTS information for the title set our title is in.
     */
    priv->vts_file = ifoOpen( priv->dvd, priv->tt_srpt->title[ title ].title_set_nr );
    if( !priv->vts_file ) {
        fprintf( stderr, "Can't open the title %d info file.\n",
                 priv->tt_srpt->title[ title ].title_set_nr );
        ifoClose( priv->vmg_file );
        DVDClose( priv->dvd );
        return -1;
    }


    /**
     * Determine which program chain we want to watch.  This is based on the
     * chapter number.
     */
    priv->ttn = priv->tt_srpt->title[ title ].vts_ttn;
    priv->vts_ptt_srpt = priv->vts_file->vts_ptt_srpt;
    priv->pgc_id = priv->vts_ptt_srpt->title[ priv->ttn - 1 ].ptt[ chapter ].pgcn;
    priv->pgn = priv->vts_ptt_srpt->title[ priv->ttn - 1 ].ptt[ chapter ].pgn;
    priv->cur_pgc = priv->vts_file->vts_pgcit->pgci_srp[ priv->pgc_id - 1 ].pgc;
    priv->start_cell = priv->cur_pgc->program_map[ priv->pgn - 1 ] - 1;


    /**
     * We've got enough info, time to open the title set data.
     */
    priv->dvd_title = DVDOpenFile( priv->dvd, priv->tt_srpt->title[ title ].title_set_nr,
                         DVD_READ_TITLE_VOBS );
    if( !priv->dvd_title ) {
        fprintf( stderr, "Can't open title VOBS (VTS_%02d_1.VOB).\n",
                 priv->tt_srpt->title[ title ].title_set_nr );
        ifoClose( priv->vts_file );
        ifoClose( priv->vmg_file );
        DVDClose( priv->dvd );
        return -1;
    }

    return 0;
}

static void
dvdreadsrc_loop (GstElement *element) 
{
  DVDReadSrc *dvdreadsrc;
  DVDReadSrcPrivate *priv;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_DVDREADSRC (element));

  dvdreadsrc = DVDREADSRC (element);
  priv = dvdreadsrc->priv;
  g_return_if_fail (GST_FLAG_IS_SET (dvdreadsrc, DVDREADSRC_OPEN));

  /**
   * Playback by cell in this pgc, starting at the cell for our chapter.
   */
  priv->next_cell = priv->start_cell;
  for( priv->cur_cell = priv->start_cell; priv->next_cell < priv->cur_pgc->nr_of_cells; ) {

      priv->cur_cell = priv->next_cell;

      /* Check if we're entering an angle block. */
      if( priv->cur_pgc->cell_playback[ priv->cur_cell ].block_type
                                        == BLOCK_TYPE_ANGLE_BLOCK ) {
          int i;

          priv->cur_cell += priv->angle;
          for( i = 0;; ++i ) {
              if( priv->cur_pgc->cell_playback[ priv->cur_cell + i ].block_mode
                                        == BLOCK_MODE_LAST_CELL ) {
                  priv->next_cell = priv->cur_cell + i + 1;
                  break;
              }
          }
      } else {
          priv->next_cell = priv->cur_cell + 1;
      }


      /**
       * We loop until we're out of this cell.
       */
      for( priv->cur_pack = priv->cur_pgc->cell_playback[ priv->cur_cell ].first_sector;
           priv->cur_pack < priv->cur_pgc->cell_playback[ priv->cur_cell ].last_sector; ) {

          dsi_t dsi_pack;
          unsigned int next_vobu, next_ilvu_start, cur_output_size;
          GstBuffer *buf;
          unsigned char *data;
          int len;

          /* create the buffer */
          /* FIXME: should eventually use a bufferpool for this */
          buf = gst_buffer_new ();
          g_return_if_fail (buf);

          /* allocate the space for the buffer data */
          data = g_malloc (1024 * DVD_VIDEO_LB_LEN);
          GST_BUFFER_DATA (buf) = data;

          g_return_if_fail (GST_BUFFER_DATA (buf) != NULL);

          /**
           * Read NAV packet.
           */
          len = DVDReadBlocks( priv->dvd_title, priv->cur_pack, 1, data );
          if( len == 0 ) {
              fprintf( stderr, "Read failed for block %d\n", priv->cur_pack );
              _close(priv);
              gst_element_set_eos (GST_ELEMENT (dvdreadsrc));
              return;
          }
          assert( is_nav_pack( data ) );


          /**
           * Parse the contained dsi packet.
           */
          navRead_DSI( &dsi_pack, &(data[ DSI_START_BYTE ]) );
          assert( priv->cur_pack == dsi_pack.dsi_gi.nv_pck_lbn );
          /*navPrint_DSI(&dsi_pack); */


          /**
           * Determine where we go next.  These values are the ones we mostly
           * care about.
           */
          next_ilvu_start = priv->cur_pack
                            + dsi_pack.sml_agli.data[ priv->angle ].address;
          cur_output_size = dsi_pack.dsi_gi.vobu_ea;


          /**
           * If we're not at the end of this cell, we can determine the next
           * VOBU to display using the VOBU_SRI information section of the
           * DSI.  Using this value correctly follows the current angle,
           * avoiding the doubled scenes in The Matrix, and makes our life
           * really happy.
           *
           * Otherwise, we set our next address past the end of this cell to
           * force the code above to go to the next cell in the program.
           */
          if( dsi_pack.vobu_sri.next_vobu != SRI_END_OF_CELL ) {
              next_vobu = priv->cur_pack
                          + ( dsi_pack.vobu_sri.next_vobu & 0x7fffffff );
          } else {
              next_vobu = priv->cur_pack + cur_output_size + 1;
          }

          assert( cur_output_size < 1024 );
          priv->cur_pack++;

          /**
           * Read in and output cursize packs.
           */
          len = DVDReadBlocks( priv->dvd_title, priv->cur_pack,
                                       cur_output_size, data );
          if( len != cur_output_size ) {
              fprintf( stderr, "Read failed for %d blocks at %d\n",
                       cur_output_size, priv->cur_pack );
              _close(priv);
              gst_element_set_eos (GST_ELEMENT (dvdreadsrc));
              return;
          }

          GST_BUFFER_SIZE(buf) = cur_output_size * DVD_VIDEO_LB_LEN;
          gst_pad_push(priv->srcpad, buf);
          priv->cur_pack = next_vobu;
      }
  }
}

#if 0
static int
_read(DVDReadSrcPrivate *priv, int angle, int new_seek, GstBuffer *buf)
{
    unsigned char *data;

    data = GST_BUFFER_DATA(buf);

    /**
     * Playback by cell in this pgc, starting at the cell for our chapter.
     */
    if (new_seek) {
      priv->next_cell = priv->start_cell;
      priv->cur_cell = priv->start_cell;
    }
    if (priv->next_cell < priv->cur_pgc->nr_of_cells) {

        priv->cur_cell = priv->next_cell;

        /* Check if we're entering an angle block. */
        if( priv->cur_pgc->cell_playback[ priv->cur_cell ].block_type
                                        == BLOCK_TYPE_ANGLE_BLOCK ) {
            int i;

            priv->cur_cell += angle;
            for( i = 0;; ++i ) {
                if( priv->cur_pgc->cell_playback[ priv->cur_cell + i ].block_mode
                                          == BLOCK_MODE_LAST_CELL ) {
                    priv->next_cell = priv->cur_cell + i + 1;
                    break;
                }
            }
        } else {
            priv->next_cell = priv->cur_cell + 1;
        }


        /**
         * We loop until we're out of this cell.
         */
        if (priv->new_cell) {
          priv->cur_pack = priv->cur_pgc->cell_playback[ priv->cur_cell ].first_sector;
          priv->new_cell = FALSE;
        }

        if (priv->cur_pack < priv->cur_pgc->cell_playback[ priv->cur_cell ].last_sector; ) {

            dsi_t dsi_pack;
            unsigned int next_vobu, next_ilvu_start, cur_output_size;
            int len;


            /**
             * Read NAV packet.
             */
            len = DVDReadBlocks( priv->title, priv->cur_pack, 1, data );
            if( len == 0 ) {
                fprintf( stderr, "Read failed for block %d\n", priv->cur_pack );
                _close(priv);
                return -1;
            }
            assert( is_nav_pack( data ) );


            /**
             * Parse the contained dsi packet.
             */
            navRead_DSI( &dsi_pack, &(data[ DSI_START_BYTE ]), sizeof(dsi_t) );
            assert( priv->cur_pack == dsi_pack.dsi_gi.nv_pck_lbn );


            /**
             * Determine where we go next.  These values are the ones we mostly
             * care about.
             */
            next_ilvu_start = priv->cur_pack
                              + dsi_pack.sml_agli.data[ angle ].address;
            cur_output_size = dsi_pack.dsi_gi.vobu_ea;


            /**
             * If we're not at the end of this cell, we can determine the next
             * VOBU to display using the VOBU_SRI information section of the
             * DSI.  Using this value correctly follows the current angle,
             * avoiding the doubled scenes in The Matrix, and makes our life
             * really happy.
             *
             * Otherwise, we set our next address past the end of this cell to
             * force the code above to go to the next cell in the program.
             */
            if( dsi_pack.vobu_sri.next_vobu != SRI_END_OF_CELL ) {
                next_vobu = priv->cur_pack
                            + ( dsi_pack.vobu_sri.next_vobu & 0x7fffffff );
            } else {
                next_vobu = priv->cur_pack + cur_output_size + 1;
            }

            assert( cur_output_size < 1024 );
            priv->cur_pack++;

            /**
             * Read in and output cursize packs.
             */
            len = DVDReadBlocks( priv->title, priv->cur_pack,
                                         cur_output_size, data );
            if( len != cur_output_size ) {
                fprintf( stderr, "Read failed for %d blocks at %d\n",
                         cur_output_size, priv->cur_pack );
                _close(priv);
                return -1;
            }

            GST_BUFFER_SIZE(buf) = cur_output_size * DVD_VIDEO_LB_LEN;
            priv->cur_pack = next_vobu;
        }
    } else {
        return -1;
    }

    return 0;
}

static GstBuffer *
dvdreadsrc_get (GstPad *pad) 
{
  DVDReadSrc *dvdreadsrc;
  DVDReadSrcPrivate *priv;
  GstBuffer *buf;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  dvdreadsrc = DVDREADSRC (gst_pad_get_parent (pad));
  priv = dvdreadsrc->priv;
  g_return_val_if_fail (GST_FLAG_IS_SET (dvdreadsrc, DVDREADSRC_OPEN),NULL);

  /* create the buffer */
  /* FIXME: should eventually use a bufferpool for this */
  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);

  /* allocate the space for the buffer data */
  GST_BUFFER_DATA (buf) = g_malloc (1024 * DVD_VIDEO_LB_LEN);
  g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

  if (priv->new_seek) {
    _seek(priv, priv->titleid, priv->chapid, priv->angle);
  }

  /* read it in from the file */
  if (_read (priv, priv->angle, priv->new_seek, buf)) {
    gst_element_signal_eos (GST_ELEMENT (dvdreadsrc));
    return NULL;
  }

  if (priv->new_seek) {
    priv->new_seek = FALSE;
  }

  return buf;
}
#endif

/* open the file, necessary to go to RUNNING state */
static gboolean 
dvdreadsrc_open_file (DVDReadSrc *src) 
{
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_DVDREADSRC(src), FALSE);
  g_return_val_if_fail (!GST_FLAG_IS_SET (src, DVDREADSRC_OPEN), FALSE);

  if (_open(src->priv, src->priv->location))
    return FALSE;
  if (_seek(src->priv,
      src->priv->title,
      src->priv->chapter,
      src->priv->angle))
    return FALSE;

  GST_FLAG_SET (src, DVDREADSRC_OPEN);
  
  return TRUE;
}

/* close the file */
static void 
dvdreadsrc_close_file (DVDReadSrc *src) 
{
  g_return_if_fail (GST_FLAG_IS_SET (src, DVDREADSRC_OPEN));

  _close(src->priv);

  GST_FLAG_UNSET (src, DVDREADSRC_OPEN);
}

static GstElementStateReturn
dvdreadsrc_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_DVDREADSRC (element), GST_STATE_FAILURE);

  GST_DEBUG (0,"gstdvdreadsrc: state pending %d", GST_STATE_PENDING (element));

  /* if going down into NULL state, close the file if it's open */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, DVDREADSRC_OPEN))
      dvdreadsrc_close_file (DVDREADSRC (element));
  /* otherwise (READY or higher) we need to open the file */
  } else {
    if (!GST_FLAG_IS_SET (element, DVDREADSRC_OPEN)) {
      if (!dvdreadsrc_open_file (DVDREADSRC (element)))
        return GST_STATE_FAILURE;
    }
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the dvdreadsrc element */
  factory = gst_element_factory_new ("dvdreadsrc", GST_TYPE_DVDREADSRC,
                                    &dvdreadsrc_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "dvdreadsrc",
  plugin_init
};

