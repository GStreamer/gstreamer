/*
 * GStreamer
 * Copyright (c) 2002 Tom Barry  All rights reserved.
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

/*
 * Relicensed for GStreamer from GPL to LGPL with permit from Tom Barry.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */


#ifndef TopFirst
#define TopFirst IsOdd
#endif

#ifdef SEFUNC
#undef SEFUNC
#endif

#if defined(IS_SSE)
#define SEFUNC(x) Search_Effort_SSE_##x()
#elif defined(IS_3DNOW)
#define SEFUNC(x) Search_Effort_3DNOW_##x()
#else
#define SEFUNC(x) Search_Effort_MMX_##x()
#endif

#include "TomsMoCompAll2.inc"

#define USE_STRANGE_BOB

#include "TomsMoCompAll2.inc"

#undef USE_STRANGE_BOB


void FUNCT_NAME(GstDeinterlace2* object)
{
  pMyMemcpy = object->pMemcpy;
  
  /* double stride do address just every odd/even scanline */
  src_pitch = object->field_stride;
  dst_pitch = object->output_stride;
  rowsize   = object->line_length;
  FldHeight = object->field_height;

  pCopySrc   = GST_BUFFER_DATA(object->field_history[object->history_count-1].buf);
  pCopySrcP  = GST_BUFFER_DATA(object->field_history[object->history_count-3].buf);
  pWeaveSrc  = GST_BUFFER_DATA(object->field_history[object->history_count-2].buf);  
  pWeaveSrcP = GST_BUFFER_DATA(object->field_history[object->history_count-4].buf);

  /* use bottom field and interlace top field */
  if (object->field_history[object->history_count-2].flags == PICTURE_INTERLACED_BOTTOM) {
    IsOdd      = 1;

    // if we have an odd field we copy an even field and weave an odd field
    pCopyDest = GST_BUFFER_DATA(object->out_buf);
    pWeaveDest = pCopyDest + dst_pitch;
  }
  /* do it vice verca */
  else {

    IsOdd      = 0;
    // if we have an even field we copy an odd field and weave an even field
    pCopyDest = GST_BUFFER_DATA(object->out_buf) + dst_pitch;
    pWeaveDest = GST_BUFFER_DATA(object->out_buf);
  }

  
#ifdef IS_SSE2
  // SSE2 support temporarily deleted
#endif

  // copy 1st and last weave lines 
  Fieldcopy(pWeaveDest, pCopySrc, rowsize,		
	    1, dst_pitch*2, src_pitch);
  Fieldcopy(pWeaveDest+(FldHeight-1)*dst_pitch*2,
	    pCopySrc+(FldHeight-1)*src_pitch, rowsize, 
	    1, dst_pitch*2, src_pitch);
  
#ifdef USE_VERTICAL_FILTER
  // Vertical Filter currently not implemented for DScaler !!
  // copy 1st and last lines the copy field
  Fieldcopy(pCopyDest, pCopySrc, rowsize, 
	    1, dst_pitch*2, src_pitch);
  Fieldcopy(pCopyDest+(FldHeight-1)*dst_pitch*2,
	    pCopySrc+(FldHeight-1)*src_pitch, rowsize, 
	    1, dst_pitch*2, src_pitch);
#else
  
  // copy all of the copy field
  Fieldcopy(pCopyDest, pCopySrc, rowsize, 
	    FldHeight, dst_pitch*2, src_pitch);
#endif	
  // then go fill in the hard part, being variously lazy depending upon
  // SearchEffort

  if(!UseStrangeBob) {
    if (SearchEffort == 0)
      {
	SEFUNC(0);
      }
    else if (SearchEffort <= 1)
      {
	SEFUNC(1);
      }
    /*	else if (SearchEffort <= 2)
	{
	SEFUNC(2);
	}
    */
    else if (SearchEffort <= 3)
      {
	SEFUNC(3);
      }
    else if (SearchEffort <= 5)
      {
	SEFUNC(5);
      }
    else if (SearchEffort <= 9)
      {
	SEFUNC(9);
      }
    else if (SearchEffort <= 11)
      {
	SEFUNC(11);
      }
    else if (SearchEffort <= 13)
      {
	SEFUNC(13);
      }
    else if (SearchEffort <= 15)
      {
	SEFUNC(15);
      }
    else if (SearchEffort <= 19)
      {
	SEFUNC(19);
      }
    else if (SearchEffort <= 21)
      {
	SEFUNC(21);
      }
    else 
      {
	SEFUNC(Max);
      }
  }
  else
    {
      if (SearchEffort == 0)
	{
	  SEFUNC(0_SB);
	}
      else if (SearchEffort <= 1)
	{
	  SEFUNC(1_SB);
	}
      /*	else if (SearchEffort <= 2)
		{
		SEFUNC(2_SB);
		}
      */
      else if (SearchEffort <= 3)
	{
	  SEFUNC(3_SB);
	}
      else if (SearchEffort <= 5)
	{
	  SEFUNC(5_SB);
	}
      else if (SearchEffort <= 9)
	{
	  SEFUNC(9_SB);
	}
      else if (SearchEffort <= 11)
	{
	  SEFUNC(11_SB);
	}
      else if (SearchEffort <= 13)
	{
	  SEFUNC(13_SB);
	}
      else if (SearchEffort <= 15)
	{
	  SEFUNC(15_SB);
	}
      else if (SearchEffort <= 19)
	{
	  SEFUNC(19_SB);
	}
      else if (SearchEffort <= 21)
	{
	  SEFUNC(21_SB);
	}
      else 
	{
	  SEFUNC(Max_SB);
	}
    }

#ifdef ARCH_386
  __asm__ __volatile__("emms");
#endif
}
