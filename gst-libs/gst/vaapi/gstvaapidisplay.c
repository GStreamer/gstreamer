/*
 *  gstvaapidisplay.c - VA display abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2013 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapidisplay
 * @short_description: VA display abstraction
 */

#include "sysdeps.h"
#include <string.h>
#include "gstvaapiutils.h"
#include "gstvaapivalue.h"
#include "gstvaapidisplay.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiworkarounds.h"
#include "gstvaapiversion.h"

#define DEBUG 1
#include "gstvaapidebug.h"

GST_DEBUG_CATEGORY(gst_debug_vaapi);

G_DEFINE_TYPE(GstVaapiDisplay, gst_vaapi_display, G_TYPE_OBJECT)

typedef struct _GstVaapiConfig GstVaapiConfig;
struct _GstVaapiConfig {
    GstVaapiProfile     profile;
    GstVaapiEntrypoint  entrypoint;
};

typedef struct _GstVaapiProperty GstVaapiProperty;
struct _GstVaapiProperty {
    const gchar        *name;
    VADisplayAttribute  attribute;
    gint                old_value;
};

typedef struct _GstVaapiFormatInfo GstVaapiFormatInfo;
struct _GstVaapiFormatInfo {
    GstVaapiImageFormat format;
    guint               flags;
};

#define DEFAULT_RENDER_MODE     GST_VAAPI_RENDER_MODE_TEXTURE
#define DEFAULT_ROTATION        GST_VAAPI_ROTATION_0

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_DISPLAY_TYPE,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_RENDER_MODE,
    PROP_ROTATION,
    PROP_HUE,
    PROP_SATURATION,
    PROP_BRIGHTNESS,
    PROP_CONTRAST,

    N_PROPERTIES
};

static GstVaapiDisplayCache *g_display_cache = NULL;

static GParamSpec *g_properties[N_PROPERTIES] = { NULL, };

static gboolean
get_attribute(GstVaapiDisplay *display, VADisplayAttribType type, gint *value);

static gboolean
set_attribute(GstVaapiDisplay *display, VADisplayAttribType type, gint value);

static gboolean
get_color_balance(GstVaapiDisplay *display, guint prop_id, gfloat *v);

static gboolean
set_color_balance(GstVaapiDisplay *display, guint prop_id, gfloat v);

static void
libgstvaapi_init_once(void)
{
    static gsize g_once = FALSE;

    if (!g_once_init_enter(&g_once))
        return;

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapi, "vaapi", 0, "VA-API helper");

    /* Dump gstreamer-vaapi version for debugging purposes */
    GST_INFO("gstreamer-vaapi version %s", GST_VAAPI_VERSION_ID);

    g_once_init_leave(&g_once, TRUE);
}

static inline GstVaapiDisplayCache *
get_display_cache(void)
{
    if (!g_display_cache)
        g_display_cache = gst_vaapi_display_cache_new();
    return g_display_cache;
}

GstVaapiDisplayCache *
gst_vaapi_display_get_cache(void)
{
    return get_display_cache();
}

static void
free_display_cache(void)
{
    if (!g_display_cache)
        return;
    if (gst_vaapi_display_cache_get_size(g_display_cache) > 0)
        return;
    gst_vaapi_display_cache_free(g_display_cache);
    g_display_cache = NULL;
}

/* GstVaapiDisplayType enumerations */
GType
gst_vaapi_display_type_get_type(void)
{
    static GType g_type = 0;

    static const GEnumValue display_types[] = {
        { GST_VAAPI_DISPLAY_TYPE_ANY,
          "Auto detection", "any" },
#if USE_X11
        { GST_VAAPI_DISPLAY_TYPE_X11,
          "VA/X11 display", "x11" },
#endif
#if USE_GLX
        { GST_VAAPI_DISPLAY_TYPE_GLX,
          "VA/GLX display", "glx" },
#endif
#if USE_WAYLAND
        { GST_VAAPI_DISPLAY_TYPE_WAYLAND,
          "VA/Wayland display", "wayland" },
#endif
#if USE_DRM
        { GST_VAAPI_DISPLAY_TYPE_DRM,
          "VA/DRM display", "drm" },
#endif
        { 0, NULL, NULL },
    };

    if (!g_type)
        g_type = g_enum_register_static("GstVaapiDisplayType", display_types);
    return g_type;
}

/* Append GstVaapiImageFormat to formats array */
static inline void
append_format(GArray *formats, GstVaapiImageFormat format, guint flags)
{
    GstVaapiFormatInfo fi;

    fi.format = format;
    fi.flags = flags;
    g_array_append_val(formats, fi);
}

/* Append VAImageFormats to formats array */
static void
append_formats(GArray *formats, const VAImageFormat *va_formats,
    guint *flags, guint n)
{
    GstVaapiImageFormat format;
    const GstVaapiFormatInfo *YV12_fip = NULL;
    const GstVaapiFormatInfo *I420_fip = NULL;
    guint i;

    for (i = 0; i < n; i++) {
        const VAImageFormat * const va_format = &va_formats[i];
        const GstVaapiFormatInfo **fipp;

        format = gst_vaapi_image_format(va_format);
        if (!format) {
            GST_DEBUG("unsupported format %" GST_FOURCC_FORMAT,
                      GST_FOURCC_ARGS(va_format->fourcc));
            continue;
        }
        append_format(formats, format, flags ? flags[i] : 0);

        switch (format) {
        case GST_VAAPI_IMAGE_YV12:
            fipp = &YV12_fip;
            break;
        case GST_VAAPI_IMAGE_I420:
            fipp = &I420_fip;
            break;
        default:
            fipp = NULL;
            break;
        }
        if (fipp)
            *fipp = &g_array_index(formats, GstVaapiFormatInfo,
                formats->len - 1);
    }

    /* Append I420 (resp. YV12) format if YV12 (resp. I420) is not
       supported by the underlying driver */
    if (YV12_fip && !I420_fip)
        append_format(formats, GST_VAAPI_IMAGE_I420, YV12_fip->flags);
    else if (I420_fip && !YV12_fip)
        append_format(formats, GST_VAAPI_IMAGE_YV12, I420_fip->flags);
}

/* Sort image formats. Prefer YUV formats first */
static gint
compare_yuv_formats(gconstpointer a, gconstpointer b)
{
    const GstVaapiImageFormat fmt1 = ((GstVaapiFormatInfo *)a)->format;
    const GstVaapiImageFormat fmt2 = ((GstVaapiFormatInfo *)b)->format;

    const gboolean is_fmt1_yuv = gst_vaapi_image_format_is_yuv(fmt1);
    const gboolean is_fmt2_yuv = gst_vaapi_image_format_is_yuv(fmt2);

    if (is_fmt1_yuv != is_fmt2_yuv)
        return is_fmt1_yuv ? -1 : 1;

    return ((gint)gst_vaapi_image_format_get_score(fmt1) -
            (gint)gst_vaapi_image_format_get_score(fmt2));
}

/* Sort subpicture formats. Prefer RGB formats first */
static gint
compare_rgb_formats(gconstpointer a, gconstpointer b)
{
    const GstVaapiImageFormat fmt1 = ((GstVaapiFormatInfo *)a)->format;
    const GstVaapiImageFormat fmt2 = ((GstVaapiFormatInfo *)b)->format;

    const gboolean is_fmt1_rgb = gst_vaapi_image_format_is_rgb(fmt1);
    const gboolean is_fmt2_rgb = gst_vaapi_image_format_is_rgb(fmt2);

    if (is_fmt1_rgb != is_fmt2_rgb)
        return is_fmt1_rgb ? -1 : 1;

    return ((gint)gst_vaapi_image_format_get_score(fmt1) -
            (gint)gst_vaapi_image_format_get_score(fmt2));
}

/* Check if configs array contains profile at entrypoint */
static inline gboolean
find_config(
    GArray             *configs,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint
)
{
    GstVaapiConfig *config;
    guint i;

    if (!configs)
        return FALSE;

    for (i = 0; i < configs->len; i++) {
        config = &g_array_index(configs, GstVaapiConfig, i);
        if (config->profile == profile && config->entrypoint == entrypoint)
            return TRUE;
    }
    return FALSE;
}

/* HACK: append H.263 Baseline profile if MPEG-4:2 Simple profile is supported */
static void
append_h263_config(GArray *configs)
{
    GstVaapiConfig *config, tmp_config;
    GstVaapiConfig *mpeg4_simple_config = NULL;
    GstVaapiConfig *h263_baseline_config = NULL;
    guint i;

    if (!WORKAROUND_H263_BASELINE_DECODE_PROFILE)
        return;

    if (!configs)
        return;

    for (i = 0; i < configs->len; i++) {
        config = &g_array_index(configs, GstVaapiConfig, i);
        if (config->profile == GST_VAAPI_PROFILE_MPEG4_SIMPLE)
            mpeg4_simple_config = config;
        else if (config->profile == GST_VAAPI_PROFILE_H263_BASELINE)
            h263_baseline_config = config;
    }

    if (mpeg4_simple_config && !h263_baseline_config) {
        tmp_config = *mpeg4_simple_config;
        tmp_config.profile = GST_VAAPI_PROFILE_H263_BASELINE;
        g_array_append_val(configs, tmp_config);
    }
}

/* Convert configs array to profiles as GstCaps */
static GstCaps *
get_profile_caps(GArray *configs)
{
    GstVaapiConfig *config;
    GstCaps *out_caps, *caps;
    guint i;

    if (!configs)
        return NULL;

    out_caps = gst_caps_new_empty();
    if (!out_caps)
        return NULL;

    for (i = 0; i < configs->len; i++) {
        config = &g_array_index(configs, GstVaapiConfig, i);
        caps   = gst_vaapi_profile_get_caps(config->profile);
        if (caps)
            gst_caps_merge(out_caps, caps);
    }
    return out_caps;
}

/* Find format info */
static const GstVaapiFormatInfo *
find_format_info(GArray *formats, GstVaapiImageFormat format)
{
    const GstVaapiFormatInfo *fip;
    guint i;

    for (i = 0; i < formats->len; i++) {
        fip = &g_array_index(formats, GstVaapiFormatInfo, i);
        if (fip->format == format)
            return fip;
    }
    return NULL;
}

/* Check if formats array contains format */
static inline gboolean
find_format(GArray *formats, GstVaapiImageFormat format)
{
    return find_format_info(formats, format) != NULL;
}

/* Convert formats array to GstCaps */
static GstCaps *
get_format_caps(GArray *formats)
{
    const GstVaapiFormatInfo *fip;
    GstCaps *out_caps, *caps;
    guint i;

    out_caps = gst_caps_new_empty();
    if (!out_caps)
        return NULL;

    for (i = 0; i < formats->len; i++) {
        fip = &g_array_index(formats, GstVaapiFormatInfo, i);
        caps = gst_vaapi_image_format_get_caps(fip->format);
        if (caps)
            gst_caps_append(out_caps, caps);
    }
    return out_caps;
}

/* Find display attribute */
static const GstVaapiProperty *
find_property(GArray *properties, const gchar *name)
{
    GstVaapiProperty *prop;
    guint i;

    if (!name)
        return NULL;

    for (i = 0; i < properties->len; i++) {
        prop = &g_array_index(properties, GstVaapiProperty, i);
        if (strcmp(prop->name, name) == 0)
            return prop;
    }
    return NULL;
}

#if 0
static const GstVaapiProperty *
find_property_by_type(GArray *properties, VADisplayAttribType type)
{
    GstVaapiProperty *prop;
    guint i;

    for (i = 0; i < properties->len; i++) {
        prop = &g_array_index(properties, GstVaapiProperty, i);
        if (prop->attribute.type == type)
            return prop;
    }
    return NULL;
}
#endif

static inline const GstVaapiProperty *
find_property_by_pspec(GstVaapiDisplay *display, GParamSpec *pspec)
{
    return find_property(display->priv->properties, pspec->name);
}

static void
gst_vaapi_display_calculate_pixel_aspect_ratio(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate * const priv = display->priv;
    gdouble ratio, delta;
    gint i, j, index, windex;

    static const gint par[][2] = {
        {1, 1},         /* regular screen            */
        {16, 15},       /* PAL TV                    */
        {11, 10},       /* 525 line Rec.601 video    */
        {54, 59},       /* 625 line Rec.601 video    */
        {64, 45},       /* 1280x1024 on 16:9 display */
        {5, 3},         /* 1280x1024 on  4:3 display */
        {4, 3}          /*  800x600  on 16:9 display */
    };

    /* First, calculate the "real" ratio based on the X values;
     * which is the "physical" w/h divided by the w/h in pixels of the
     * display */
    if (!priv->width || !priv->height || !priv->width_mm || !priv->height_mm)
        ratio = 1.0;
    else
        ratio = (gdouble)(priv->width_mm * priv->height) /
            (priv->height_mm * priv->width);
    GST_DEBUG("calculated pixel aspect ratio: %f", ratio);

    /* Now, find the one from par[][2] with the lowest delta to the real one */
#define DELTA(idx, w) (ABS(ratio - ((gdouble)par[idx][w] / par[idx][!(w)])))
    delta  = DELTA(0, 0);
    index  = 0;
    windex = 0;

    for (i = 1; i < G_N_ELEMENTS(par); i++) {
        for (j = 0; j < 2; j++) {
            const gdouble this_delta = DELTA(i, j);
            if (this_delta < delta) {
                index  = i;
                windex = j;
                delta  = this_delta;
            }
        }
    }
#undef DELTA

    priv->par_n = par[index][windex];
    priv->par_d = par[index][windex ^ 1];
}

static void
gst_vaapi_display_destroy(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate * const priv = display->priv;

    if (priv->decoders) {
        g_array_free(priv->decoders, TRUE);
        priv->decoders = NULL;
    }

    if (priv->encoders) {
        g_array_free(priv->encoders, TRUE);
        priv->encoders = NULL;
    }

    if (priv->image_formats) {
        g_array_free(priv->image_formats, TRUE);
        priv->image_formats = NULL;
    }

    if (priv->subpicture_formats) {
        g_array_free(priv->subpicture_formats, TRUE);
        priv->subpicture_formats = NULL;
    }

    if (priv->properties) {
        g_array_free(priv->properties, TRUE);
        priv->properties = NULL;
    }

    if (priv->display) {
        if (!priv->parent)
            vaTerminate(priv->display);
        priv->display = NULL;
    }

    if (priv->create_display) {
        GstVaapiDisplayClass *klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
        if (klass->close_display)
            klass->close_display(display);
    }

    g_clear_object(&priv->parent);

    if (g_display_cache) {
        gst_vaapi_display_cache_remove(get_display_cache(), display);
        free_display_cache();
    }
}

static gboolean
gst_vaapi_display_create(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate * const priv = display->priv;
    GstVaapiDisplayCache *cache;
    gboolean            has_errors      = TRUE;
    VADisplayAttribute *display_attrs   = NULL;
    VAProfile          *profiles        = NULL;
    VAEntrypoint       *entrypoints     = NULL;
    VAImageFormat      *formats         = NULL;
    unsigned int       *flags           = NULL;
    gint                i, j, n, num_entrypoints, major_version, minor_version;
    VAStatus            status;
    GstVaapiDisplayInfo info;
    const GstVaapiDisplayInfo *cached_info = NULL;

    memset(&info, 0, sizeof(info));
    info.display = display;
    info.display_type = priv->display_type;

    if (priv->display)
        info.va_display = priv->display;
    else if (priv->create_display) {
        GstVaapiDisplayClass *klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
        if (klass->open_display && !klass->open_display(display))
            return FALSE;
        if (!klass->get_display || !klass->get_display(display, &info))
            return FALSE;
        priv->display = info.va_display;
        priv->display_type = info.display_type;
        if (klass->get_size)
            klass->get_size(display, &priv->width, &priv->height);
        if (klass->get_size_mm)
            klass->get_size_mm(display, &priv->width_mm, &priv->height_mm);
        gst_vaapi_display_calculate_pixel_aspect_ratio(display);
    }
    if (!priv->display)
        return FALSE;

    cache = get_display_cache();
    if (!cache)
        return FALSE;
    cached_info = gst_vaapi_display_cache_lookup_by_va_display(
        cache,
        info.va_display
    );
    if (cached_info) {
        g_clear_object(&priv->parent);
        priv->parent = g_object_ref(cached_info->display);
        priv->display_type = cached_info->display_type;
    }

    if (!priv->parent) {
        status = vaInitialize(priv->display, &major_version, &minor_version);
        if (!vaapi_check_status(status, "vaInitialize()"))
            goto end;
        GST_DEBUG("VA-API version %d.%d", major_version, minor_version);
    }

    /* VA profiles */
    profiles = g_new(VAProfile, vaMaxNumProfiles(priv->display));
    if (!profiles)
        goto end;
    entrypoints = g_new(VAEntrypoint, vaMaxNumEntrypoints(priv->display));
    if (!entrypoints)
        goto end;
    status = vaQueryConfigProfiles(priv->display, profiles, &n);
    if (!vaapi_check_status(status, "vaQueryConfigProfiles()"))
        goto end;

    GST_DEBUG("%d profiles", n);
    for (i = 0; i < n; i++) {
#if VA_CHECK_VERSION(0,34,0)
        /* Introduced in VA/VPP API */
        if (profiles[i] == VAProfileNone)
            continue;
#endif
        GST_DEBUG("  %s", string_of_VAProfile(profiles[i]));
    }

    priv->decoders = g_array_new(FALSE, FALSE, sizeof(GstVaapiConfig));
    if (!priv->decoders)
        goto end;
    priv->encoders = g_array_new(FALSE, FALSE, sizeof(GstVaapiConfig));
    if (!priv->encoders)
        goto end;

    for (i = 0; i < n; i++) {
        GstVaapiConfig config;

        config.profile = gst_vaapi_profile(profiles[i]);
        if (!config.profile)
            continue;

        status = vaQueryConfigEntrypoints(
            priv->display,
            profiles[i],
            entrypoints, &num_entrypoints
        );
        if (!vaapi_check_status(status, "vaQueryConfigEntrypoints()"))
            continue;

        for (j = 0; j < num_entrypoints; j++) {
            config.entrypoint = gst_vaapi_entrypoint(entrypoints[j]);
            switch (config.entrypoint) {
            case GST_VAAPI_ENTRYPOINT_VLD:
            case GST_VAAPI_ENTRYPOINT_IDCT:
            case GST_VAAPI_ENTRYPOINT_MOCO:
                g_array_append_val(priv->decoders, config);
                break;
            case GST_VAAPI_ENTRYPOINT_SLICE_ENCODE:
                g_array_append_val(priv->encoders, config);
                break;
            }
        }
    }
    append_h263_config(priv->decoders);

    /* VA display attributes */
    display_attrs =
        g_new(VADisplayAttribute, vaMaxNumDisplayAttributes(priv->display));
    if (!display_attrs)
        goto end;

    n = 0; /* XXX: workaround old GMA500 bug */
    status = vaQueryDisplayAttributes(priv->display, display_attrs, &n);
    if (!vaapi_check_status(status, "vaQueryDisplayAttributes()"))
        goto end;

    priv->properties = g_array_new(FALSE, FALSE, sizeof(GstVaapiProperty));
    if (!priv->properties)
        goto end;

    GST_DEBUG("%d display attributes", n);
    for (i = 0; i < n; i++) {
        VADisplayAttribute * const attr = &display_attrs[i];
        GstVaapiProperty prop;
        gint value;

        GST_DEBUG("  %s", string_of_VADisplayAttributeType(attr->type));

        switch (attr->type) {
#if !VA_CHECK_VERSION(0,34,0)
        case VADisplayAttribDirectSurface:
            prop.name = GST_VAAPI_DISPLAY_PROP_RENDER_MODE;
            break;
#endif
        case VADisplayAttribRenderMode:
            prop.name = GST_VAAPI_DISPLAY_PROP_RENDER_MODE;
            break;
        case VADisplayAttribRotation:
            prop.name = GST_VAAPI_DISPLAY_PROP_ROTATION;
            break;
        case VADisplayAttribHue:
            prop.name = GST_VAAPI_DISPLAY_PROP_HUE;
            break;
        case VADisplayAttribSaturation:
            prop.name = GST_VAAPI_DISPLAY_PROP_SATURATION;
            break;
        case VADisplayAttribBrightness:
            prop.name = GST_VAAPI_DISPLAY_PROP_BRIGHTNESS;
            break;
        case VADisplayAttribContrast:
            prop.name = GST_VAAPI_DISPLAY_PROP_CONTRAST;
            break;
        default:
            prop.name = NULL;
            break;
        }
        if (!prop.name)
            continue;

        /* Assume the attribute is really supported if we can get the
         * actual and current value */
        if (!get_attribute(display, attr->type, &value))
            continue;

        /* Some drivers (e.g. EMGD) have completely random initial
         * values */
        if (value < attr->min_value || value > attr->max_value)
            continue;

        prop.attribute = *attr;
        prop.old_value = value;
        g_array_append_val(priv->properties, prop);
    }

    /* VA image formats */
    formats = g_new(VAImageFormat, vaMaxNumImageFormats(priv->display));
    if (!formats)
        goto end;
    status = vaQueryImageFormats(priv->display, formats, &n);
    if (!vaapi_check_status(status, "vaQueryImageFormats()"))
        goto end;

    GST_DEBUG("%d image formats", n);
    for (i = 0; i < n; i++)
        GST_DEBUG("  %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS(formats[i].fourcc));

    priv->image_formats =
        g_array_new(FALSE, FALSE, sizeof(GstVaapiFormatInfo));
    if (!priv->image_formats)
        goto end;
    append_formats(priv->image_formats, formats, NULL, n);
    g_array_sort(priv->image_formats, compare_yuv_formats);

    /* VA subpicture formats */
    n = vaMaxNumSubpictureFormats(priv->display);
    formats = g_renew(VAImageFormat, formats, n);
    flags   = g_new(guint, n);
    if (!formats || !flags)
        goto end;
    status = vaQuerySubpictureFormats(priv->display, formats, flags, (guint *)&n);
    if (!vaapi_check_status(status, "vaQuerySubpictureFormats()"))
        goto end;

    GST_DEBUG("%d subpicture formats", n);
    for (i = 0; i < n; i++) {
        GST_DEBUG("  %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS(formats[i].fourcc));
        flags[i] = to_GstVaapiSubpictureFlags(flags[i]);
    }

    priv->subpicture_formats =
        g_array_new(FALSE, FALSE, sizeof(GstVaapiFormatInfo));
    if (!priv->subpicture_formats)
        goto end;
    append_formats(priv->subpicture_formats, formats, flags, n);
    g_array_sort(priv->subpicture_formats, compare_rgb_formats);

    if (!cached_info) {
        if (!gst_vaapi_display_cache_add(cache, &info))
            goto end;
    }

    has_errors = FALSE;
end:
    g_free(display_attrs);
    g_free(profiles);
    g_free(entrypoints);
    g_free(formats);
    g_free(flags);
    return !has_errors;
}

static void
gst_vaapi_display_lock_default(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate *priv = display->priv;

    if (priv->parent)
        priv = priv->parent->priv;
    g_rec_mutex_lock(&priv->mutex);
}

static void
gst_vaapi_display_unlock_default(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate *priv = display->priv;

    if (priv->parent)
        priv = priv->parent->priv;
    g_rec_mutex_unlock(&priv->mutex);
}

static void
gst_vaapi_display_finalize(GObject *object)
{
    GstVaapiDisplay * const display = GST_VAAPI_DISPLAY(object);

    gst_vaapi_display_destroy(display);

    g_rec_mutex_clear(&display->priv->mutex);

    G_OBJECT_CLASS(gst_vaapi_display_parent_class)->finalize(object);
}

static void
gst_vaapi_display_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDisplay * const display = GST_VAAPI_DISPLAY(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        display->priv->display = g_value_get_pointer(value);
        break;
    case PROP_DISPLAY_TYPE:
        display->priv->display_type = g_value_get_enum(value);
        break;
    case PROP_RENDER_MODE:
        gst_vaapi_display_set_render_mode(display, g_value_get_enum(value));
        break;
    case PROP_ROTATION:
        gst_vaapi_display_set_rotation(display, g_value_get_enum(value));
        break;
    case PROP_HUE:
    case PROP_SATURATION:
    case PROP_BRIGHTNESS:
    case PROP_CONTRAST:
        set_color_balance(display, prop_id, g_value_get_float(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDisplay * const display = GST_VAAPI_DISPLAY(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_pointer(value, gst_vaapi_display_get_display(display));
        break;
    case PROP_DISPLAY_TYPE:
        g_value_set_enum(value, gst_vaapi_display_get_display_type(display));
        break;
    case PROP_WIDTH:
        g_value_set_uint(value, gst_vaapi_display_get_width(display));
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, gst_vaapi_display_get_height(display));
        break;
    case PROP_RENDER_MODE: {
        GstVaapiRenderMode mode;
        if (!gst_vaapi_display_get_render_mode(display, &mode))
            mode = DEFAULT_RENDER_MODE;
        g_value_set_enum(value, mode);
        break;
    }
    case PROP_ROTATION:
        g_value_set_enum(value, gst_vaapi_display_get_rotation(display));
        break;
    case PROP_HUE:
    case PROP_SATURATION:
    case PROP_BRIGHTNESS:
    case PROP_CONTRAST: {
        gfloat v;
        if (!get_color_balance(display, prop_id, &v))
            v = G_PARAM_SPEC_FLOAT(pspec)->default_value;
        g_value_set_float(value, v);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_constructed(GObject *object)
{
    GstVaapiDisplay * const display = GST_VAAPI_DISPLAY(object);
    GObjectClass *parent_class;

    display->priv->create_display = display->priv->display == NULL;
    if (!gst_vaapi_display_create(display))
        gst_vaapi_display_destroy(display);

    parent_class = G_OBJECT_CLASS(gst_vaapi_display_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
gst_vaapi_display_class_init(GstVaapiDisplayClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDisplayClass * const dpy_class = GST_VAAPI_DISPLAY_CLASS(klass);

    libgstvaapi_init_once();

    g_type_class_add_private(klass, sizeof(GstVaapiDisplayPrivate));

    object_class->finalize      = gst_vaapi_display_finalize;
    object_class->set_property  = gst_vaapi_display_set_property;
    object_class->get_property  = gst_vaapi_display_get_property;
    object_class->constructed   = gst_vaapi_display_constructed;

    dpy_class->lock             = gst_vaapi_display_lock_default;
    dpy_class->unlock           = gst_vaapi_display_unlock_default;

    g_properties[PROP_DISPLAY] =
        g_param_spec_pointer("display",
                             "VA display",
                             "VA display",
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);

    g_properties[PROP_DISPLAY_TYPE] =
        g_param_spec_enum("display-type",
                          "VA display type",
                          "VA display type",
                          GST_VAAPI_TYPE_DISPLAY_TYPE,
                          GST_VAAPI_DISPLAY_TYPE_ANY,
                          G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);

    g_properties[PROP_WIDTH] =
        g_param_spec_uint("width",
                          "Width",
                          "The display width",
                          1, G_MAXUINT32, 1,
                          G_PARAM_READABLE);

    g_properties[PROP_HEIGHT] =
        g_param_spec_uint("height",
                          "height",
                          "The display height",
                          1, G_MAXUINT32, 1,
                          G_PARAM_READABLE);

    /**
     * GstVaapiDisplay:render-mode:
     *
     * The VA display rendering mode, expressed as a #GstVaapiRenderMode.
     */
    g_properties[PROP_RENDER_MODE] =
        g_param_spec_enum(GST_VAAPI_DISPLAY_PROP_RENDER_MODE,
                          "render mode",
                          "The display rendering mode",
                          GST_VAAPI_TYPE_RENDER_MODE,
                          DEFAULT_RENDER_MODE,
                          G_PARAM_READWRITE);

    /**
     * GstVaapiDisplay:rotation:
     *
     * The VA display rotation mode, expressed as a #GstVaapiRotation.
     */
    g_properties[PROP_ROTATION] =
        g_param_spec_enum(GST_VAAPI_DISPLAY_PROP_ROTATION,
                          "rotation",
                          "The display rotation mode",
                          GST_VAAPI_TYPE_ROTATION,
                          DEFAULT_ROTATION,
                          G_PARAM_READWRITE);

    /**
     * GstVaapiDisplay:hue:
     *
     * The VA display hue, expressed as a float value. Range is -180.0
     * to 180.0. Default value is 0.0 and represents no modification.
     */
    g_properties[PROP_HUE] =
        g_param_spec_float(GST_VAAPI_DISPLAY_PROP_HUE,
                           "hue",
                           "The display hue value",
                           -180.0, 180.0, 0.0,
                           G_PARAM_READWRITE);

    /**
     * GstVaapiDisplay:saturation:
     *
     * The VA display saturation, expressed as a float value. Range is
     * 0.0 to 2.0. Default value is 1.0 and represents no modification.
     */
    g_properties[PROP_SATURATION] =
        g_param_spec_float(GST_VAAPI_DISPLAY_PROP_SATURATION,
                           "saturation",
                           "The display saturation value",
                           0.0, 2.0, 1.0,
                           G_PARAM_READWRITE);

    /**
     * GstVaapiDisplay:brightness:
     *
     * The VA display brightness, expressed as a float value. Range is
     * -1.0 to 1.0. Default value is 0.0 and represents no modification.
     */
    g_properties[PROP_BRIGHTNESS] =
        g_param_spec_float(GST_VAAPI_DISPLAY_PROP_BRIGHTNESS,
                           "brightness",
                           "The display brightness value",
                           -1.0, 1.0, 0.0,
                           G_PARAM_READWRITE);

    /**
     * GstVaapiDisplay:contrast:
     *
     * The VA display contrast, expressed as a float value. Range is
     * 0.0 to 2.0. Default value is 1.0 and represents no modification.
     */
    g_properties[PROP_CONTRAST] =
        g_param_spec_float(GST_VAAPI_DISPLAY_PROP_CONTRAST,
                           "contrast",
                           "The display contrast value",
                           0.0, 2.0, 1.0,
                           G_PARAM_READWRITE);

    g_object_class_install_properties(object_class, N_PROPERTIES, g_properties);
}

static void
gst_vaapi_display_init(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate *priv = GST_VAAPI_DISPLAY_GET_PRIVATE(display);

    display->priv               = priv;
    priv->parent                = NULL;
    priv->display_type          = GST_VAAPI_DISPLAY_TYPE_ANY;
    priv->display               = NULL;
    priv->width                 = 0;
    priv->height                = 0;
    priv->width_mm              = 0;
    priv->height_mm             = 0;
    priv->par_n                 = 1;
    priv->par_d                 = 1;
    priv->decoders              = NULL;
    priv->encoders              = NULL;
    priv->image_formats         = NULL;
    priv->subpicture_formats    = NULL;
    priv->properties            = NULL;
    priv->create_display        = TRUE;

    g_rec_mutex_init(&priv->mutex);
}

/**
 * gst_vaapi_display_new_with_display:
 * @va_display: a #VADisplay
 *
 * Creates a new #GstVaapiDisplay, using @va_display as the VA
 * display.
 *
 * Return value: the newly created #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_new_with_display(VADisplay va_display)
{
    GstVaapiDisplayCache * const cache = get_display_cache();
    const GstVaapiDisplayInfo *info;

    g_return_val_if_fail(va_display != NULL, NULL);
    g_return_val_if_fail(cache != NULL, NULL);

    info = gst_vaapi_display_cache_lookup_by_va_display(cache, va_display);
    if (info)
        return g_object_ref(info->display);

    return g_object_new(GST_VAAPI_TYPE_DISPLAY,
                        "display", va_display,
                        NULL);
}

/**
 * gst_vaapi_display_lock:
 * @display: a #GstVaapiDisplay
 *
 * Locks @display. If @display is already locked by another thread,
 * the current thread will block until @display is unlocked by the
 * other thread.
 */
void
gst_vaapi_display_lock(GstVaapiDisplay *display)
{
    GstVaapiDisplayClass *klass;

    g_return_if_fail(GST_VAAPI_IS_DISPLAY(display));

    klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
    if (klass->lock)
        klass->lock(display);
}

/**
 * gst_vaapi_display_unlock:
 * @display: a #GstVaapiDisplay
 *
 * Unlocks @display. If another thread is blocked in a
 * gst_vaapi_display_lock() call for @display, it will be woken and
 * can lock @display itself.
 */
void
gst_vaapi_display_unlock(GstVaapiDisplay *display)
{
    GstVaapiDisplayClass *klass;

    g_return_if_fail(GST_VAAPI_IS_DISPLAY(display));

    klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
    if (klass->unlock)
        klass->unlock(display);
}

/**
 * gst_vaapi_display_sync:
 * @display: a #GstVaapiDisplay
 *
 * Flushes any requests queued for the windowing system and waits until
 * all requests have been handled. This is often used for making sure
 * that the display is synchronized with the current state of the program.
 *
 * This is most useful for X11. On windowing systems where requests are
 * handled synchronously, this function will do nothing.
 */
void
gst_vaapi_display_sync(GstVaapiDisplay *display)
{
    GstVaapiDisplayClass *klass;

    g_return_if_fail(GST_VAAPI_IS_DISPLAY(display));

    klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
    if (klass->sync)
        klass->sync(display);
    else if (klass->flush)
        klass->flush(display);
}

/**
 * gst_vaapi_display_flush:
 * @display: a #GstVaapiDisplay
 *
 * Flushes any requests queued for the windowing system.
 *
 * This is most useful for X11. On windowing systems where requests
 * are handled synchronously, this function will do nothing.
 */
void
gst_vaapi_display_flush(GstVaapiDisplay *display)
{
    GstVaapiDisplayClass *klass;

    g_return_if_fail(GST_VAAPI_IS_DISPLAY(display));

    klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
    if (klass->flush)
        klass->flush(display);
}

/**
 * gst_vaapi_display_get_display:
 * @display: a #GstVaapiDisplay
 *
 * Returns the #GstVaapiDisplayType bound to @display.
 *
 * Return value: the #GstVaapiDisplayType
 */
GstVaapiDisplayType
gst_vaapi_display_get_display_type(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display),
                         GST_VAAPI_DISPLAY_TYPE_ANY);

    return display->priv->display_type;
}

/**
 * gst_vaapi_display_get_display:
 * @display: a #GstVaapiDisplay
 *
 * Returns the #VADisplay bound to @display.
 *
 * Return value: the #VADisplay
 */
VADisplay
gst_vaapi_display_get_display(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    return display->priv->display;
}

/**
 * gst_vaapi_display_get_width:
 * @display: a #GstVaapiDisplay
 *
 * Retrieves the width of a #GstVaapiDisplay.
 *
 * Return value: the width of the @display, in pixels
 */
guint
gst_vaapi_display_get_width(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), 0);

    return display->priv->width;
}

/**
 * gst_vaapi_display_get_height:
 * @display: a #GstVaapiDisplay
 *
 * Retrieves the height of a #GstVaapiDisplay
 *
 * Return value: the height of the @display, in pixels
 */
guint
gst_vaapi_display_get_height(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), 0);

    return display->priv->height;
}

/**
 * gst_vaapi_display_get_size:
 * @display: a #GstVaapiDisplay
 * @pwidth: return location for the width, or %NULL
 * @pheight: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiDisplay.
 */
void
gst_vaapi_display_get_size(GstVaapiDisplay *display, guint *pwidth, guint *pheight)
{
    g_return_if_fail(GST_VAAPI_DISPLAY(display));

    if (pwidth)
        *pwidth = display->priv->width;

    if (pheight)
        *pheight = display->priv->height;
}

/**
 * gst_vaapi_display_get_pixel_aspect_ratio:
 * @display: a #GstVaapiDisplay
 * @par_n: return location for the numerator of pixel aspect ratio, or %NULL
 * @par_d: return location for the denominator of pixel aspect ratio, or %NULL
 *
 * Retrieves the pixel aspect ratio of a #GstVaapiDisplay.
 */
void
gst_vaapi_display_get_pixel_aspect_ratio(
    GstVaapiDisplay *display,
    guint           *par_n,
    guint           *par_d
)
{
    g_return_if_fail(GST_VAAPI_IS_DISPLAY(display));

    if (par_n)
        *par_n = display->priv->par_n;

    if (par_d)
        *par_d = display->priv->par_d;
}

/**
 * gst_vaapi_display_get_decode_caps:
 * @display: a #GstVaapiDisplay
 *
 * Gets the supported profiles for decoding as #GstCaps capabilities.
 *
 * Return value: a newly allocated #GstCaps object, possibly empty
 */
GstCaps *
gst_vaapi_display_get_decode_caps(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    return get_profile_caps(display->priv->decoders);
}

/**
 * gst_vaapi_display_has_decoder:
 * @display: a #GstVaapiDisplay
 * @profile: a #VAProfile
 * @entrypoint: a #GstVaaiEntrypoint
 *
 * Returns whether VA @display supports @profile for decoding at the
 * specified @entrypoint.
 *
 * Return value: %TRUE if VA @display supports @profile for decoding.
 */
gboolean
gst_vaapi_display_has_decoder(
    GstVaapiDisplay    *display,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);

    return find_config(display->priv->decoders, profile, entrypoint);
}

/**
 * gst_vaapi_display_get_encode_caps:
 * @display: a #GstVaapiDisplay
 *
 * Gets the supported profiles for decoding as #GstCaps capabilities.
 *
 * Return value: a newly allocated #GstCaps object, possibly empty
 */
GstCaps *
gst_vaapi_display_get_encode_caps(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    return get_profile_caps(display->priv->encoders);
}

/**
 * gst_vaapi_display_has_encoder:
 * @display: a #GstVaapiDisplay
 * @profile: a #VAProfile
 * @entrypoint: a #GstVaapiEntrypoint
 *
 * Returns whether VA @display supports @profile for encoding at the
 * specified @entrypoint.
 *
 * Return value: %TRUE if VA @display supports @profile for encoding.
 */
gboolean
gst_vaapi_display_has_encoder(
    GstVaapiDisplay    *display,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);

    return find_config(display->priv->encoders, profile, entrypoint);
}

/**
 * gst_vaapi_display_get_image_caps:
 * @display: a #GstVaapiDisplay
 *
 * Gets the supported image formats for gst_vaapi_surface_get_image()
 * or gst_vaapi_surface_put_image() as #GstCaps capabilities.
 *
 * Note that this method does not necessarily map image formats
 * returned by vaQueryImageFormats(). The set of capabilities can be
 * stripped down, if gstreamer-vaapi does not support the format, or
 * expanded to cover compatible formats not exposed by the underlying
 * driver. e.g. I420 can be supported even if the driver only exposes
 * YV12.
 *
 * Return value: a newly allocated #GstCaps object, possibly empty
 */
GstCaps *
gst_vaapi_display_get_image_caps(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    return get_format_caps(display->priv->image_formats);
}

/**
 * gst_vaapi_display_has_image_format:
 * @display: a #GstVaapiDisplay
 * @format: a #GstVaapiFormat
 *
 * Returns whether VA @display supports @format image format.
 *
 * Return value: %TRUE if VA @display supports @format image format
 */
gboolean
gst_vaapi_display_has_image_format(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);
    g_return_val_if_fail(format, FALSE);

    if (find_format(display->priv->image_formats, format))
        return TRUE;

    /* XXX: try subpicture formats since some drivers could report a
     * set of VA image formats that is not a superset of the set of VA
     * subpicture formats
     */
    return find_format(display->priv->subpicture_formats, format);
}

/**
 * gst_vaapi_display_get_subpicture_caps:
 * @display: a #GstVaapiDisplay
 *
 * Gets the supported subpicture formats as #GstCaps capabilities.
 *
 * Note that this method does not necessarily map subpicture formats
 * returned by vaQuerySubpictureFormats(). The set of capabilities can
 * be stripped down if gstreamer-vaapi does not support the
 * format. e.g. this is the case for paletted formats like IA44.
 *
 * Return value: a newly allocated #GstCaps object, possibly empty
 */
GstCaps *
gst_vaapi_display_get_subpicture_caps(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    return get_format_caps(display->priv->subpicture_formats);
}

/**
 * gst_vaapi_display_has_subpicture_format:
 * @display: a #GstVaapiDisplay
 * @format: a #GstVaapiFormat
 * @flags_ptr: pointer to #GstVaapiSubpictureFlags, or zero
 *
 * Returns whether VA @display supports @format subpicture format with
 * the supplied @flags.
 *
 * Return value: %TRUE if VA @display supports @format subpicture format
 */
gboolean
gst_vaapi_display_has_subpicture_format(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format,
    guint              *flags_ptr
)
{
    const GstVaapiFormatInfo *fip;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);
    g_return_val_if_fail(format, FALSE);

    fip = find_format_info(display->priv->subpicture_formats, format);
    if (!fip)
        return FALSE;

    if (flags_ptr)
        *flags_ptr = fip->flags;
    return TRUE;
}

/**
 * gst_vaapi_display_has_property:
 * @display: a #GstVaapiDisplay
 * @name: the property name to check
 *
 * Returns whether VA @display supports the requested property. The
 * check is performed against the property @name. So, the client
 * application may perform this check only once and cache this
 * information.
 *
 * Return value: %TRUE if VA @display supports property @name
 */
gboolean
gst_vaapi_display_has_property(GstVaapiDisplay *display, const gchar *name)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);
    g_return_val_if_fail(name, FALSE);

    return find_property(display->priv->properties, name) != NULL;
}

static gboolean
get_attribute(GstVaapiDisplay *display, VADisplayAttribType type, gint *value)
{
    VADisplayAttribute attr;
    VAStatus status;

    attr.type  = type;
    attr.flags = VA_DISPLAY_ATTRIB_GETTABLE;
    status = vaGetDisplayAttributes(display->priv->display, &attr, 1);
    if (!vaapi_check_status(status, "vaGetDisplayAttributes()"))
        return FALSE;
    *value = attr.value;
    return TRUE;
}

static gboolean
set_attribute(GstVaapiDisplay *display, VADisplayAttribType type, gint value)
{
    VADisplayAttribute attr;
    VAStatus status;

    attr.type  = type;
    attr.value = value;
    attr.flags = VA_DISPLAY_ATTRIB_SETTABLE;
    status = vaSetDisplayAttributes(display->priv->display, &attr, 1);
    if (!vaapi_check_status(status, "vaSetDisplayAttributes()"))
        return FALSE;
    return TRUE;
}

static gboolean
get_render_mode_VADisplayAttribRenderMode(
    GstVaapiDisplay    *display,
    GstVaapiRenderMode *pmode
)
{
    gint modes, devices;

    if (!get_attribute(display, VADisplayAttribRenderDevice, &devices))
        return FALSE;
    if (!devices)
        return FALSE;
    if (!get_attribute(display, VADisplayAttribRenderMode, &modes))
        return FALSE;

    /* Favor "overlay" mode since it is the most restrictive one */
    if (modes & (VA_RENDER_MODE_LOCAL_OVERLAY|VA_RENDER_MODE_EXTERNAL_OVERLAY))
        *pmode = GST_VAAPI_RENDER_MODE_OVERLAY;
    else
        *pmode = GST_VAAPI_RENDER_MODE_TEXTURE;
    return TRUE;
}

static gboolean
get_render_mode_VADisplayAttribDirectSurface(
    GstVaapiDisplay    *display,
    GstVaapiRenderMode *pmode
)
{
#if VA_CHECK_VERSION(0,34,0)
    /* VADisplayAttribDirectsurface was removed in VA-API >= 0.34.0 */
    return FALSE;
#else
    gint direct_surface;

    if (!get_attribute(display, VADisplayAttribDirectSurface, &direct_surface))
        return FALSE;
    if (direct_surface)
        *pmode = GST_VAAPI_RENDER_MODE_OVERLAY;
    else
        *pmode = GST_VAAPI_RENDER_MODE_TEXTURE;
    return TRUE;
#endif
}

static gboolean
get_render_mode_default(
    GstVaapiDisplay    *display,
    GstVaapiRenderMode *pmode
)
{
    switch (display->priv->display_type) {
#if USE_WAYLAND
    case GST_VAAPI_DISPLAY_TYPE_WAYLAND:
        /* wl_buffer mapped from VA surface through vaGetSurfaceBufferWl() */
        *pmode = GST_VAAPI_RENDER_MODE_OVERLAY;
        break;
#endif
#if USE_DRM
    case GST_VAAPI_DISPLAY_TYPE_DRM:
        /* vaGetSurfaceBufferDRM() returns the underlying DRM buffer handle */
        *pmode = GST_VAAPI_RENDER_MODE_OVERLAY;
        break;
#endif
    default:
        /* This includes VA/X11 and VA/GLX modes */
        *pmode = DEFAULT_RENDER_MODE;
        break;
    }
    return TRUE;
}

/**
 * gst_vaapi_display_get_render_mode:
 * @display: a #GstVaapiDisplay
 * @pmode: return location for the VA @display rendering mode
 *
 * Returns the current VA @display rendering mode.
 *
 * Return value: %TRUE if VA @display rendering mode could be determined
 */
gboolean
gst_vaapi_display_get_render_mode(
    GstVaapiDisplay    *display,
    GstVaapiRenderMode *pmode
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);

    /* Try with render-mode attribute */
    if (get_render_mode_VADisplayAttribRenderMode(display, pmode))
        return TRUE;

    /* Try with direct-surface attribute */
    if (get_render_mode_VADisplayAttribDirectSurface(display, pmode))
        return TRUE;

    /* Default: determine from the display type */
    return get_render_mode_default(display, pmode);
}

/**
 * gst_vaapi_display_set_render_mode:
 * @display: a #GstVaapiDisplay
 * @mode: the #GstVaapiRenderMode to set
 *
 * Sets the VA @display rendering mode to the supplied @mode. This
 * function returns %FALSE if the rendering mode could not be set,
 * e.g. run-time switching rendering mode is not supported.
 *
 * Return value: %TRUE if VA @display rendering @mode could be changed
 *   to the requested value
 */
gboolean
gst_vaapi_display_set_render_mode(
    GstVaapiDisplay   *display,
    GstVaapiRenderMode mode
)
{
    gint modes, devices;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);

    if (!get_attribute(display, VADisplayAttribRenderDevice, &devices))
        return FALSE;

    modes = 0;
    switch (mode) {
    case GST_VAAPI_RENDER_MODE_OVERLAY:
        if (devices & VA_RENDER_DEVICE_LOCAL)
            modes |= VA_RENDER_MODE_LOCAL_OVERLAY;
        if (devices & VA_RENDER_DEVICE_EXTERNAL)
            modes |= VA_RENDER_MODE_EXTERNAL_OVERLAY;
        break;
    case GST_VAAPI_RENDER_MODE_TEXTURE:
        if (devices & VA_RENDER_DEVICE_LOCAL)
            modes |= VA_RENDER_MODE_LOCAL_GPU;
        if (devices & VA_RENDER_DEVICE_EXTERNAL)
            modes |= VA_RENDER_MODE_EXTERNAL_GPU;
        break;
    }
    if (!modes)
        return FALSE;
    if (!set_attribute(display, VADisplayAttribRenderMode, modes))
        return FALSE;

    g_object_notify_by_pspec(G_OBJECT(display), g_properties[PROP_RENDER_MODE]);
    return TRUE;
}

/**
 * gst_vaapi_display_get_rotation:
 * @display: a #GstVaapiDisplay
 *
 * Returns the current VA @display rotation angle. If the VA driver
 * does not support "rotation" display attribute, then the display is
 * assumed to be un-rotated.
 *
 * Return value: the current #GstVaapiRotation value
 */
GstVaapiRotation
gst_vaapi_display_get_rotation(GstVaapiDisplay *display)
{
    gint value;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), DEFAULT_ROTATION);

    if (!get_attribute(display, VADisplayAttribRotation, &value))
        value = VA_ROTATION_NONE;
    return to_GstVaapiRotation(value);
}

/**
 * gst_vaapi_display_set_rotation:
 * @display: a #GstVaapiDisplay
 * @rotation: the #GstVaapiRotation value to set
 *
 * Sets the VA @display rotation angle to the supplied @rotation
 * value. This function returns %FALSE if the rotation angle could not
 * be set, e.g. the VA driver does not allow to change the display
 * rotation angle.
 *
 * Return value: %TRUE if VA @display rotation angle could be changed
 *   to the requested value
 */
gboolean
gst_vaapi_display_set_rotation(
    GstVaapiDisplay *display,
    GstVaapiRotation rotation
)
{
    guint value;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);

    value = from_GstVaapiRotation(rotation);
    if (!set_attribute(display, VADisplayAttribRotation, value))
        return FALSE;

    g_object_notify_by_pspec(G_OBJECT(display), g_properties[PROP_ROTATION]);
    return TRUE;
}

/* Get color balance attributes */
static gboolean
get_color_balance(GstVaapiDisplay *display, guint prop_id, gfloat *v)
{
    GParamSpecFloat * const pspec = G_PARAM_SPEC_FLOAT(g_properties[prop_id]);
    const GstVaapiProperty *prop;
    const VADisplayAttribute *attr;
    gfloat out_value;
    gint value;

    if (!pspec)
        return FALSE;

    prop = find_property_by_pspec(display, &pspec->parent_instance);
    if (!prop)
        return FALSE;
    attr = &prop->attribute;

    if (!get_attribute(display, attr->type, &value))
        return FALSE;

    /* Scale wrt. the medium ("default") value */
    out_value = pspec->default_value;
    if (value > attr->value)
        out_value += ((gfloat)(value - attr->value) /
                      (attr->max_value - attr->value) *
                      (pspec->maximum - pspec->default_value));
    else if (value < attr->value)
        out_value -= ((gfloat)(attr->value - value) /
                      (attr->value - attr->min_value) *
                      (pspec->default_value - pspec->minimum));
    *v = out_value;
    return TRUE;
}

/* Set color balance attribute */
static gboolean
set_color_balance(GstVaapiDisplay *display, guint prop_id, gfloat v)
{
    GParamSpecFloat * const pspec = G_PARAM_SPEC_FLOAT(g_properties[prop_id]);
    const GstVaapiProperty *prop;
    const VADisplayAttribute *attr;
    gint value;

    if (!pspec)
        return FALSE;

    prop = find_property_by_pspec(display, &pspec->parent_instance);
    if (!prop)
        return FALSE;
    attr = &prop->attribute;

    /* Scale wrt. the medium ("default") value */
    value = attr->value;
    if (v > pspec->default_value)
        value += ((v - pspec->default_value) /
                  (pspec->maximum - pspec->default_value) *
                  (attr->max_value - attr->value));
    else if (v < pspec->default_value)
        value -= ((pspec->default_value - v) /
                  (pspec->default_value - pspec->minimum) *
                  (attr->value - attr->min_value));
    if (!set_attribute(display, attr->type, value))
        return FALSE;

    g_object_notify_by_pspec(G_OBJECT(display), g_properties[prop_id]);
    return TRUE;
}
