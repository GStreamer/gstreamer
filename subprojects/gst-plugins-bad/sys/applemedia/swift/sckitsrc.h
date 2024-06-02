/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
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

#ifndef __GST_SCKIT_SRC_H__
#define __GST_SCKIT_SRC_H__

// Including SCKit here to avoid hitting the issue below due to SCStreamOutputType being used in the header
// https://stackoverflow.com/questions/54392256/no-type-or-protocol-named-avcapturevideodataoutputsamplebufferdelegate-in-swi
// Best would be to just hide the SCKitSrc protocol from the header (only Swift parts need it) but that doesn't seem possible.
#include <ScreenCaptureKit/ScreenCaptureKit.h>
#include "GstSCKitSrc-Swift.h"

#endif /* __GST_SCKIT_SRC_H__ */