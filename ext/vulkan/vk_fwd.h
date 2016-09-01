/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifndef _VK_FWD_H_
#define _VK_FWD_H_

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstVulkanInstance GstVulkanInstance;
typedef struct _GstVulkanInstanceClass GstVulkanInstanceClass;
typedef struct _GstVulkanInstancePrivate GstVulkanInstancePrivate;

typedef struct _GstVulkanDevice GstVulkanDevice;
typedef struct _GstVulkanDeviceClass GstVulkanDeviceClass;
typedef struct _GstVulkanDevicePrivate GstVulkanDevicePrivate;

typedef struct _GstVulkanQueue GstVulkanQueue;
typedef struct _GstVulkanQueueClass GstVulkanQueueClass;

typedef enum _GstVulkanDisplayType GstVulkanDisplayType;

typedef struct _GstVulkanDisplay GstVulkanDisplay;
typedef struct _GstVulkanDisplayClass GstVulkanDisplayClass;
typedef struct _GstVulkanDisplayPrivate GstVulkanDisplayPrivate;

typedef struct _GstVulkanWindow GstVulkanWindow;
typedef struct _GstVulkanWindowClass GstVulkanWindowClass;
typedef struct _GstVulkanWindowPrivate GstVulkanWindowPrivate;

typedef struct _GstVulkanSwapper GstVulkanSwapper;
typedef struct _GstVulkanSwapperClass GstVulkanSwapperClass;
typedef struct _GstVulkanSwapperPrivate GstVulkanSwapperPrivate;

typedef struct _GstVulkanTrash GstVulkanTrash;

typedef struct _GstVulkanFence GstVulkanFence;

typedef struct _GstVulkanMemory GstVulkanMemory;
typedef struct _GstVulkanMemoryAllocator GstVulkanMemoryAllocator;
typedef struct _GstVulkanMemoryAllocatorClass GstVulkanMemoryAllocatorClass;

typedef struct _GstVulkanBufferMemory GstVulkanBufferMemory;
typedef struct _GstVulkanBufferMemoryAllocator GstVulkanBufferMemoryAllocator;
typedef struct _GstVulkanBufferMemoryAllocatorClass GstVulkanBufferMemoryAllocatorClass;

typedef struct _GstVulkanImageMemory GstVulkanImageMemory;
typedef struct _GstVulkanImageMemoryAllocator GstVulkanImageMemoryAllocator;
typedef struct _GstVulkanImageMemoryAllocatorClass GstVulkanImageMemoryAllocatorClass;

typedef struct _GstVulkanBufferPool GstVulkanBufferPool;
typedef struct _GstVulkanBufferPoolClass GstVulkanBufferPoolClass;
typedef struct _GstVulkanBufferPoolPrivate GstVulkanBufferPoolPrivate;

G_END_DECLS

#endif /* _VK_FWD_H_ */
