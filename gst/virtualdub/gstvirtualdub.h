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

#include <gst/gst.h>

typedef unsigned int    Pixel;
typedef unsigned int    Pixel32;
typedef unsigned char   Pixel8;
typedef int             PixCoord;
typedef int             PixDim;
typedef int             PixOffset;


#define R_MASK  (0x00ff0000)
#define G_MASK  (0x0000ff00)
#define B_MASK  (0x000000ff)
#define R_SHIFT         (16)
#define G_SHIFT          (8)
#define B_SHIFT          (0)


GType gst_xsharpen_get_type (void);
extern GstElementDetails gst_xsharpen_details;

extern GstPadTemplate *gst_virtualdub_sink_factory ();
extern GstPadTemplate *gst_virtualdub_src_factory ();
