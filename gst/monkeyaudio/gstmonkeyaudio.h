/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * Filter:
 * Copyright (C) 2000 Donald A. Graft
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
 *
 */

#ifndef __GST_MONKEYAUDIO_H__
#define __GST_MONKEYAUDIO_H__

extern "C" {
#endif /* __cplusplus */

#include <config.h>
#include <gst/gst.h>

GType gst_monkeyenc_get_type (void);
extern GstElementDetails gst_monkeyenc_details;

extern GstPadTemplate *gst_monkey_sink_factory ();
/*extern GstPadTemplate *gst_monkey_src_factory ();*/

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
