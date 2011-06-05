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

#ifndef __DIRECTX_DX_H__
#define __DIRECTX_DX_H__

#include <glib.h>
#include <gmodule.h>

G_BEGIN_DECLS

#define WM_DIRECTX WM_USER + 500

#define DIRECTX_VERSION_UNKNOWN 0

#define DIRECTX_VERSION_ENCODE_FULL(major, minor, micro) (                                                                                        \
	  ((major) * 10000)                                                                                                                             \
	+ ((minor) *   100)                                                                                                                             \
	+ ((micro) *     1))

#define DIRECTX_VERSION_ENCODE(major)                                                                                                             \
  DIRECTX_VERSION_ENCODE_FULL(major, 0, 0)

typedef enum
{
  DIRECTX_UNKNOWN   = DIRECTX_VERSION_UNKNOWN, 
  DIRECTX_9         = DIRECTX_VERSION_ENCODE(9),
  DIRECTX_10        = DIRECTX_VERSION_ENCODE(10),
  DIRECTX_10_1      = DIRECTX_VERSION_ENCODE_FULL(10, 1, 0),
  DIRECTX_11        = DIRECTX_VERSION_ENCODE(11)
} DirectXVersion;

#define DIRECTX_API(version, initialization_function, module_test, symbol_test, i18n_key, description)                                            \
  static DirectXAPIComponent DIRECTX_ ## version ## _DIRECT3D_COMPONENT = {                                                                       \
      NULL  /*api*/                                                                                                                               \
    , FALSE /*initialized*/                                                                                                                       \
    , NULL  /*initialize*/                                                                                                                        \
    , NULL  /*module*/                                                                                                                            \
    , NULL  /*module_name*/                                                                                                                       \
    , NULL  /*private_data*/                                                                                                                      \
  };                                                                                                                                              \
  static DirectXAPIComponent DIRECTX_ ## version ## _DIRECTINPUT_COMPONENT = {                                                                    \
      NULL  /*api*/                                                                                                                               \
    , FALSE /*initialized*/                                                                                                                       \
    , NULL  /*initialize*/                                                                                                                        \
    , NULL  /*module*/                                                                                                                            \
    , NULL  /*module_name*/                                                                                                                       \
    , NULL  /*private_data*/                                                                                                                      \
  };                                                                                                                                              \
  static DirectXAPIComponent DIRECTX_ ## version ## _DIRECTSOUND_COMPONENT = {                                                                    \
      NULL  /*api*/                                                                                                                               \
    , FALSE /*initialized*/                                                                                                                       \
    , NULL  /*initialize*/                                                                                                                        \
    , NULL  /*module*/                                                                                                                            \
    , NULL  /*module_name*/                                                                                                                       \
    , NULL  /*private_data*/                                                                                                                      \
  };                                                                                                                                              \
  static DirectXAPIComponent DIRECTX_ ## version ## _DIRECTWRITE_COMPONENT = {                                                                    \
      NULL  /*api*/                                                                                                                               \
    , FALSE /*initialized*/                                                                                                                       \
    , NULL  /*initialize*/                                                                                                                        \
    , NULL  /*module*/                                                                                                                            \
    , NULL  /*module_name*/                                                                                                                       \
    , NULL  /*private_data*/                                                                                                                      \
  };                                                                                                                                              \
  static DirectXAPIComponent DIRECTX_ ## version ## _DIRECT2D_COMPONENT = {                                                                       \
      NULL  /*api*/                                                                                                                               \
    , FALSE /*initialized*/                                                                                                                       \
    , NULL  /*initialize*/                                                                                                                        \
    , NULL  /*module*/                                                                                                                            \
    , NULL  /*module_name*/                                                                                                                       \
    , NULL  /*private_data*/                                                                                                                      \
  };                                                                                                                                              \
  static DirectXAPIComponent DIRECTX_ ## version ## _DIRECTCOMPUTE_COMPONENT = {                                                                  \
      NULL  /*api*/                                                                                                                               \
    , FALSE /*initialized*/                                                                                                                       \
    , NULL  /*initialize*/                                                                                                                        \
    , NULL  /*module*/                                                                                                                            \
    , NULL  /*module_name*/                                                                                                                       \
    , NULL  /*private_data*/                                                                                                                      \
  };                                                                                                                                              \
  static DirectXAPI DIRECTX_ ## version ## _API =  {                                                                                              \
      version                                                                                                                                     \
    , module_test "." G_MODULE_SUFFIX                                                                                                             \
    , symbol_test                                                                                                                                 \
    , i18n_key                                                                                                                                    \
    , description                                                                                                                                 \
    , FALSE                                                                                                                                       \
    , initialization_function                                                                                                                     \
    , &DIRECTX_ ## version ## _DIRECT3D_COMPONENT                                                                                                 \
    , &DIRECTX_ ## version ## _DIRECTINPUT_COMPONENT                                                                                              \
    , &DIRECTX_ ## version ## _DIRECTSOUND_COMPONENT                                                                                              \
    , &DIRECTX_ ## version ## _DIRECTWRITE_COMPONENT                                                                                              \
    , &DIRECTX_ ## version ## _DIRECT2D_COMPONENT                                                                                                 \
    , &DIRECTX_ ## version ## _DIRECTCOMPUTE_COMPONENT                                                                                            \
    , {NULL, NULL, NULL} /*reserved*/                                                                                                             \
  };                                                                                                                                              \
  static void init_directx_ ## version ## _supported_api(void) {                                                                                  \
    DirectXAPI* api;                                                                                                                              \
    api = &DIRECTX_ ## version ## _API;                                                                                                           \
    api->d3d->api      = api;                                                                                                                     \
    api->dinput->api   = api;                                                                                                                     \
    api->dsound->api   = api;                                                                                                                     \
    api->dwrite->api   = api;                                                                                                                     \
    api->d2d->api      = api;                                                                                                                     \
    api->dcompute->api = api;                                                                                                                     \
    directx_add_supported_api(api);                                                                                                               \
  }

#define INITIALIZE_SUPPORTED_DIRECTX_API(version)                                                                                                 \
  init_directx_ ## version ## _supported_api();

#define DIRECTX_COMPONENT_INIT(component)                                                                                                         \
  {                                                                                                                                               \
    if (component != NULL && component->initialize != NULL && !component->initialized) {                                                          \
      component->initialize(component, DIRECTX_COMPONENT_DATA(component));                                                                        \
    }                                                                                                                                             \
  }

#define DIRECTX_OPEN_COMPONENT_MODULE(component, component_module_name)                                                                           \
  {                                                                                                                                               \
    GModule* lib;                                                                                                                                 \
    if (component && component->module == NULL && (lib = g_module_open(component_module_name "." G_MODULE_SUFFIX, G_MODULE_BIND_LAZY)) != NULL) { \
        component->module_name = component_module_name "." G_MODULE_SUFFIX;                                                                       \
        component->module = lib;                                                                                                                  \
      }                                                                                                                                           \
  }

#define DIRECTX_OPEN_COMPONENT_SYMBOL(component, dispatch_table_type, component_symbol_name)                                                      \
  {                                                                                                                                               \
    gpointer symbol; \
    if (component && component->module && g_module_symbol(component->module, #component_symbol_name, &symbol)) {                                  \
      ((dispatch_table_type*)component->vtable)->component_symbol_name = symbol;                                                                  \
    }                                                                                                                                             \
  }

#define DIRECTX_CALL_COMPONENT_SYMBOL(component, dispatch_table_type, component_symbol_name, ...)                                                 \
  (((dispatch_table_type*)component->vtable)->component_symbol_name(__VA_ARGS__))


/* Borrowed from GST_FUNCTION */
#ifndef DIRECTX_FUNCTION
#if defined (__GNUC__) || (defined (_MSC_VER) && _MSC_VER >= 1300)
#  define DIRECTX_FUNCTION     ((const char*) (__FUNCTION__))
#elif defined (__STDC__) && defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#  define DIRECTX_FUNCTION     ((const char*) (__func__))
#else
#  define DIRECTX_FUNCTION     ((const char*) ("???"))
#endif
#endif

#define DIRECTX_DEBUG(...)                                              (directx_log_debug(__FILE__, DIRECTX_FUNCTION, __LINE__, __VA_ARGS__))
#define DIRECTX_WARNING(...)                                            (directx_log_warning(__FILE__, DIRECTX_FUNCTION, __LINE__, __VA_ARGS__))
#define DIRECTX_ERROR(...)                                              (directx_log_error(__FILE__, DIRECTX_FUNCTION, __LINE__, __VA_ARGS__))

#define DIRECTX_COMPONENT_API(component)                                (component->api)
#define DIRECTX_COMPONENT_DATA(component)                               (component->private_data)
#define DIRECTX_SET_COMPONENT_DATA(component, data)                     (component->private_data = data)
#define DIRECTX_SET_COMPONENT_INIT(component, init_function)            (component->initialize = init_function)
#define DIRECTX_SET_COMPONENT_DISPATCH_TABLE(component, dispatch_table) (component->vtable = dispatch_table)
#define DIRECTX_VERSION_IS_UNKNOWN(version)                             (version == DIRECTX_VERSION_UNKNOWN)
#define DIRECTX_SUPPORTED_API_IS_LAST(lib)                              (lib == NULL || lib->version == DIRECTX_VERSION_UNKNOWN || lib->module_name == NULL)

typedef struct _DirectXInitParams DirectXInitParams;
typedef struct _DirectXAPI DirectXAPI;
typedef struct _DirectXAPIComponent DirectXAPIComponent;

/* Function pointers */
typedef void (*DirectXInitializationFunction) (const DirectXAPI* api);
typedef void (*DirectXLogFunction) (const gchar* file, const gchar* function, gint line, const gchar* format, va_list args); /* vprintf-style logging function */

struct _DirectXInitParams 
{
  DirectXLogFunction log_debug;
  DirectXLogFunction log_warning;
  DirectXLogFunction log_error;
};

struct _DirectXAPI 
{
  gint                          version;
  const gchar*                  module_test;
  const gchar*                  symbol_test;
  const gchar*                  i18n_key;
  const gchar*                  description;
  gboolean                      initialized;
  DirectXInitializationFunction initialize;
  DirectXAPIComponent*          d3d;
  DirectXAPIComponent*          dinput;
  DirectXAPIComponent*          dsound;
  DirectXAPIComponent*          dwrite;
  DirectXAPIComponent*          d2d;
  DirectXAPIComponent*          dcompute;
  gpointer                      reserved[3];
};

#define DIRECTX_D3D(api)                     (api->d3d)
#define DIRECTX_DINPUT(api)                  (api->dinput)
#define DIRECTX_DSOUND(api)                  (api->dsound)
#define DIRECTX_DWRITE(api)                  (api->dwrite)
#define DIRECTX_D2D(api)                     (api->d2d)
#define DIRECTX_DCOMPUTE(api)                (api->dcompute)

#define DIRECTX_D3D_COMPONENT_DATA(api)      (DIRECTX_COMPONENT_DATA(DIRECTX_D3D(api)))
#define DIRECTX_DINPUT_COMPONENT_DATA(api)   (DIRECTX_COMPONENT_DATA(DIRECTX_DINPUT(api)))
#define DIRECTX_DSOUND_COMPONENT_DATA(api)   (DIRECTX_COMPONENT_DATA(DIRECTX_DSOUND(api)))
#define DIRECTX_DWRITE_COMPONENT_DATA(api)   (DIRECTX_COMPONENT_DATA(DIRECTX_DWRITE(api)))
#define DIRECTX_D2D_COMPONENT_DATA(api)      (DIRECTX_COMPONENT_DATA(DIRECTX_D2D(api)))
#define DIRECTX_DCOMPUTE_COMPONENT_DATA(api) (DIRECTX_COMPONENT_DATA(DIRECTX_DCOMPUTE(api)))

/* DirectX component function table */
typedef void (*DirectXComponentInitializeFunction) (DirectXAPIComponent* d3d, gpointer data);
struct _DirectXAPIComponent
{
  DirectXAPI*                        api;
  gboolean                           initialized;
  DirectXComponentInitializeFunction initialize;

  GModule*                           module;
  const gchar*                       module_name;

  gpointer                           vtable;

  gpointer                           private_data;
};

gboolean directx_initialize (DirectXInitParams* init_params);
gboolean directx_is_initialized (void);

gboolean directx_is_supported (void);

void directx_log_debug(const gchar* file, const gchar* function, gint line, const gchar * format, ...);
void directx_log_warning(const gchar* file, const gchar* function, gint line, const gchar * format, ...);
void directx_log_error(const gchar* file, const gchar* function, gint line, const gchar * format, ...);

GList* directx_get_supported_apis (void);
gint32 directx_get_supported_api_count (void);
gboolean directx_add_supported_api (DirectXAPI* api);

DirectXAPI* directx_get_best_available_api (void);
gboolean directx_initialize_best_available_api (void);
gboolean directx_best_available_api_is_initialized (void);

gboolean directx_api_initialize (DirectXAPI* api);
gboolean directx_api_is_initialized (const DirectXAPI* api);

G_END_DECLS

#endif /* __DIRECTX_DX_H__ */
