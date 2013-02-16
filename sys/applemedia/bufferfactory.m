/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#import "bufferfactory.h"

#include "coremediabuffer.h"
#include "corevideobuffer.h"

@implementation GstAMBufferFactory

- (id)initWithError:(GError **)error
{
  GstCoreMediaCtx *ctx;

  ctx =
      gst_core_media_ctx_new (GST_API_CORE_VIDEO | GST_API_CORE_MEDIA, error);
  if (ctx == NULL)
    return nil;

  if ((self = [super init]))
    coreMediaCtx = ctx;
  else
    g_object_unref (ctx);

  return self;
}

- (void)finalize
{
  g_object_unref (coreMediaCtx);

  [super finalize];
}

- (GstBuffer *)createGstBufferForCoreVideoBuffer:(CFTypeRef)cvbuf
{
  return gst_core_video_buffer_new (coreMediaCtx, (CVBufferRef) cvbuf, NULL);
}

- (GstBuffer *)createGstBufferForSampleBuffer:(CFTypeRef)sbuf
{
  return gst_core_media_buffer_new (coreMediaCtx, sbuf);
}

@end
