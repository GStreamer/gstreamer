/* GStreamer
 * Copyright (C) 2011 David Hoyt <dhoyt@hoytsoft.org>
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

#ifndef __DIRECTX_D3D_H__
#define __DIRECTX_D3D_H__

#include <glib.h>

#include "dx.h"

G_BEGIN_DECLS

#define WM_DIRECTX_D3D_INIT_DEVICE      WM_DIRECTX + 1
#define WM_DIRECTX_D3D_INIT_DEVICELOST  WM_DIRECTX + 2
#define WM_DIRECTX_D3D_DEVICELOST       WM_DIRECTX + 3
#define WM_DIRECTX_D3D_END_DEVICELOST   WM_DIRECTX + 4
#define WM_DIRECTX_D3D_RESIZE           WM_DIRECTX + 5

#define DIRECTX_D3D_API(version, dispatch_table, init_function, create_function, resize_function, device_lost_function, notify_device_reset_function, release_function) \
  static gpointer DIRECTX_API_COMPONENT_D3D_ ## version ## _DISPATCH_TABLE = &dispatch_table;                                                                           \
  static DirectXAPIComponentD3D DIRECTX_API_COMPONENT_D3D_ ## version ## _INIT = {                                                                                      \
      create_function               /*create_function*/                                                                                                                 \
    , resize_function               /*resize_function*/                                                                                                                 \
    , device_lost_function          /*device_lost_function*/                                                                                                            \
    , notify_device_reset_function  /*notify_device_reset_function*/                                                                                                    \
    , release_function              /*release_function*/                                                                                                                \
    , NULL                          /*private_data*/                                                                                                                    \
  };                                                                                                                                                                    \
  static void init_directx_api_component_d3d_ ## version ## _(const DirectXAPI* api) {                                                                                  \
    gpointer private_data = &DIRECTX_API_COMPONENT_D3D_ ## version ## _INIT;                                                                                            \
    gpointer vtable = DIRECTX_API_COMPONENT_D3D_ ## version ## _DISPATCH_TABLE;                                                                                         \
    DIRECTX_SET_COMPONENT_INIT(DIRECTX_D3D(api), init_function);                                                                                                        \
    DIRECTX_SET_COMPONENT_DATA(DIRECTX_D3D(api), private_data);                                                                                                         \
    DIRECTX_SET_COMPONENT_DISPATCH_TABLE(DIRECTX_D3D(api), vtable);                                                                                                     \
  }

#define INITIALIZE_DIRECTX_D3D_API(version, api)                                                                                                                        \
  init_directx_api_component_d3d_ ## version ## _(api);

#define DIRECTX_D3D_FUNCTIONS(d3d)                          ((DirectXAPIComponentD3D*)d3d->d3d_component)
#define DIRECTX_D3D_API_FUNCTIONS(api)                      ((DirectXAPIComponentD3D*)DIRECTX_D3D_COMPONENT_DATA(api))
#define DIRECTX_D3D_CALL_FUNCTION(d3d, func_name, ...)      (DIRECTX_D3D_FUNCTIONS(d3d)->func_name(__VA_ARGS__))
#define DIRECTX_D3D_CALL_API_FUNCTION(api, func_name, ...)  (DIRECTX_D3D_API_FUNCTIONS(api)->func_name(__VA_ARGS__))

typedef struct _DirectXD3D DirectXD3D;
typedef struct _DirectXAPIComponentD3D DirectXAPIComponentD3D;

/* Function pointers */
typedef DirectXD3D* (*DirectXD3DCreateFunction)             (const DirectXAPI* api);
typedef gboolean    (*DirectXD3DResizeFunction)             (const DirectXD3D* d3d);
typedef gboolean    (*DirectXD3DDeviceLostFunction)         (const DirectXD3D* d3d);
typedef gboolean    (*DirectXD3DNotifyDeviceResetFunction)  (const DirectXD3D* d3d);
typedef gboolean    (*DirectXD3DReleaseFunction)            (const DirectXD3D* d3d);

struct _DirectXAPIComponentD3D 
{
  DirectXD3DCreateFunction              create;
  DirectXD3DResizeFunction              resize;
  DirectXD3DDeviceLostFunction          device_lost;
  DirectXD3DNotifyDeviceResetFunction   notify_device_reset;
  DirectXD3DReleaseFunction             release;

  gpointer                              private_data;
};

struct _DirectXD3D 
{
  DirectXAPI*               api;
  DirectXAPIComponent*      api_component;
  DirectXAPIComponentD3D*   d3d_component;

  gpointer                  private_data;
};

const DirectXD3D* directx_d3d_create(const DirectXAPI* api);
gboolean directx_d3d_resize(const DirectXD3D* d3d);
gboolean directx_d3d_device_lost(const DirectXD3D* d3d);
gboolean directx_d3d_notify_device_reset(const DirectXD3D* d3d);
gboolean directx_d3d_release(const DirectXD3D* d3d);

G_END_DECLS

#endif /* __DIRECTX_D3D_H__ */
