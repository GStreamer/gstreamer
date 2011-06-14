/*
 *  gstvaapidisplay.c - VA display abstraction
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
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

#include "config.h"
#include "gstvaapiutils.h"
#include "gstvaapidisplay.h"
#include "gstvaapidisplay_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

GST_DEBUG_CATEGORY(gst_debug_vaapi);

G_DEFINE_TYPE(GstVaapiDisplay, gst_vaapi_display, G_TYPE_OBJECT);

typedef struct _GstVaapiConfig GstVaapiConfig;
struct _GstVaapiConfig {
    GstVaapiProfile     profile;
    GstVaapiEntrypoint  entrypoint;
};

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_WIDTH,
    PROP_HEIGHT
};

/* Append GstVaapiImageFormat to formats array */
static inline void
append_format(GArray *formats, GstVaapiImageFormat format)
{
    g_array_append_val(formats, format);
}

/* Append VAImageFormats to formats array */
static void
append_formats(GArray *formats, const VAImageFormat *va_formats, guint n)
{
    GstVaapiImageFormat format;
    gboolean has_YV12 = FALSE;
    gboolean has_I420 = FALSE;
    guint i;

    for (i = 0; i < n; i++) {
        const VAImageFormat * const va_format = &va_formats[i];

        format = gst_vaapi_image_format(va_format);
        if (!format) {
            GST_DEBUG("unsupported format %" GST_FOURCC_FORMAT,
                      GST_FOURCC_ARGS(va_format->fourcc));
            continue;
        }

        switch (format) {
        case GST_VAAPI_IMAGE_YV12:
            has_YV12 = TRUE;
            break;
        case GST_VAAPI_IMAGE_I420:
            has_I420 = TRUE;
            break;
        default:
            break;
        }
        append_format(formats, format);
    }

    /* Append I420 (resp. YV12) format if YV12 (resp. I420) is not
       supported by the underlying driver */
    if (has_YV12 && !has_I420)
        append_format(formats, GST_VAAPI_IMAGE_I420);
    else if (has_I420 && !has_YV12)
        append_format(formats, GST_VAAPI_IMAGE_YV12);
}

/* Sort image formats. Prefer YUV formats first */
static gint
compare_yuv_formats(gconstpointer a, gconstpointer b)
{
    const GstVaapiImageFormat fmt1 = *(GstVaapiImageFormat *)a;
    const GstVaapiImageFormat fmt2 = *(GstVaapiImageFormat *)b;

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
    const GstVaapiImageFormat fmt1 = *(GstVaapiImageFormat *)a;
    const GstVaapiImageFormat fmt2 = *(GstVaapiImageFormat *)b;

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

    for (i = 0; i < configs->len; i++) {
        config = &g_array_index(configs, GstVaapiConfig, i);
        if (config->profile == profile && config->entrypoint == entrypoint)
            return TRUE;
    }
    return FALSE;
}

/* Convert configs array to profiles as GstCaps */
static GstCaps *
get_profile_caps(GArray *configs)
{
    GstVaapiConfig *config;
    GstCaps *out_caps, *caps;
    guint i;

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

/* Check if formats array contains format */
static inline gboolean
find_format(GArray *formats, GstVaapiImageFormat format)
{
    guint i;

    for (i = 0; i < formats->len; i++)
        if (g_array_index(formats, GstVaapiImageFormat, i) == format)
            return TRUE;
    return FALSE;
}

/* Convert formats array to GstCaps */
static GstCaps *
get_format_caps(GArray *formats)
{
    GstVaapiImageFormat format;
    GstCaps *out_caps, *caps;
    guint i;

    out_caps = gst_caps_new_empty();
    if (!out_caps)
        return NULL;

    for (i = 0; i < formats->len; i++) {
        format = g_array_index(formats, GstVaapiImageFormat, i);
        caps   = gst_vaapi_image_format_get_caps(format);
        if (caps)
            gst_caps_append(out_caps, caps);
    }
    return out_caps;
}

static void
gst_vaapi_display_calculate_pixel_aspect_ratio(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate * const priv = display->priv;
    gdouble ratio, delta;
    gint i, index;

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
#define DELTA(idx) (ABS(ratio - ((gdouble)par[idx][0] / par[idx][1])))
    delta = DELTA(0);
    index = 0;

    for (i = 1; i < G_N_ELEMENTS(par); i++) {
        const gdouble this_delta = DELTA(i);
        if (this_delta < delta) {
            index = i;
            delta = this_delta;
        }
    }
#undef DELTA

    priv->par_n = par[index][0];
    priv->par_d = par[index][1];
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

    if (priv->display) {
        vaTerminate(priv->display);
        priv->display = NULL;
    }

    if (priv->create_display) {
        GstVaapiDisplayClass *klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
        if (klass->close_display)
            klass->close_display(display);
    }
}

static gboolean
gst_vaapi_display_create(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate * const priv = display->priv;
    gboolean            has_errors      = TRUE;
    VAProfile          *profiles        = NULL;
    VAEntrypoint       *entrypoints     = NULL;
    VAImageFormat      *formats         = NULL;
    unsigned int       *flags           = NULL;
    gint                i, j, n, num_entrypoints, major_version, minor_version;
    VAStatus            status;

    if (!priv->display && priv->create_display) {
        GstVaapiDisplayClass *klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
        if (klass->open_display && !klass->open_display(display))
            return FALSE;
        if (klass->get_display) {
            priv->display = klass->get_display(display);
            if (!priv->display)
                return FALSE;
        }
        if (klass->get_size)
            klass->get_size(display, &priv->width, &priv->height);
        if (klass->get_size_mm)
            klass->get_size_mm(display, &priv->width_mm, &priv->height_mm);
        gst_vaapi_display_calculate_pixel_aspect_ratio(display);
    }
    if (!priv->display)
        return FALSE;

    status = vaInitialize(priv->display, &major_version, &minor_version);
    if (!vaapi_check_status(status, "vaInitialize()"))
        goto end;
    GST_DEBUG("VA-API version %d.%d", major_version, minor_version);

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
    for (i = 0; i < n; i++)
        GST_DEBUG("  %s", string_of_VAProfile(profiles[i]));

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
            goto end;

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

    /* VA image formats */
    formats = g_new(VAImageFormat, vaMaxNumImageFormats(priv->display));
    if (!formats)
        goto end;
    status = vaQueryImageFormats(priv->display, formats, &n);
    if (!vaapi_check_status(status, "vaQueryImageFormats()"))
        goto end;

    GST_DEBUG("%d image formats", n);
    for (i = 0; i < n; i++)
        GST_DEBUG("  %s", string_of_FOURCC(formats[i].fourcc));

    priv->image_formats =
        g_array_new(FALSE, FALSE, sizeof(GstVaapiImageFormat));
    if (!priv->image_formats)
        goto end;
    append_formats(priv->image_formats, formats, n);
    g_array_sort(priv->image_formats, compare_yuv_formats);

    /* VA subpicture formats */
    n = vaMaxNumSubpictureFormats(priv->display);
    formats = g_renew(VAImageFormat, formats, n);
    flags   = g_new(guint, n);
    if (!formats || !flags)
        goto end;
    status = vaQuerySubpictureFormats(priv->display, formats, flags, &n);
    if (!vaapi_check_status(status, "vaQuerySubpictureFormats()"))
        goto end;

    GST_DEBUG("%d subpicture formats", n);
    for (i = 0; i < n; i++)
        GST_DEBUG("  %s", string_of_FOURCC(formats[i].fourcc));

    priv->subpicture_formats =
        g_array_new(FALSE, FALSE, sizeof(GstVaapiImageFormat));
    if (!priv->subpicture_formats)
        goto end;
    append_formats(priv->subpicture_formats, formats, n);
    g_array_sort(priv->subpicture_formats, compare_rgb_formats);

    has_errors = FALSE;
end:
    g_free(profiles);
    g_free(entrypoints);
    g_free(formats);
    g_free(flags);
    return !has_errors;
}

static void
gst_vaapi_display_lock_default(GstVaapiDisplay *display)
{
    g_static_rec_mutex_lock(&display->priv->mutex);
}

static void
gst_vaapi_display_unlock_default(GstVaapiDisplay *display)
{
    g_static_rec_mutex_unlock(&display->priv->mutex);
}

static void
gst_vaapi_display_finalize(GObject *object)
{
    GstVaapiDisplay * const display = GST_VAAPI_DISPLAY(object);

    gst_vaapi_display_destroy(display);

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
    case PROP_WIDTH:
        g_value_set_uint(value, gst_vaapi_display_get_width(display));
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, gst_vaapi_display_get_height(display));
        break;
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
    gst_vaapi_display_create(display);

    parent_class = G_OBJECT_CLASS(gst_vaapi_display_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
gst_vaapi_display_class_init(GstVaapiDisplayClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDisplayClass * const dpy_class = GST_VAAPI_DISPLAY_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapi, "vaapi", 0, "VA-API helper");

    g_type_class_add_private(klass, sizeof(GstVaapiDisplayPrivate));

    object_class->finalize      = gst_vaapi_display_finalize;
    object_class->set_property  = gst_vaapi_display_set_property;
    object_class->get_property  = gst_vaapi_display_get_property;
    object_class->constructed   = gst_vaapi_display_constructed;

    dpy_class->lock             = gst_vaapi_display_lock_default;
    dpy_class->unlock           = gst_vaapi_display_unlock_default;

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_pointer("display",
                              "VA display",
                              "VA display",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "Width",
                           "The display width",
                           1, G_MAXUINT32, 1,
                           G_PARAM_READABLE));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "height",
                           "The display height",
                           1, G_MAXUINT32, 1,
                           G_PARAM_READABLE));
}

static void
gst_vaapi_display_init(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate *priv = GST_VAAPI_DISPLAY_GET_PRIVATE(display);

    display->priv               = priv;
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
    priv->create_display        = TRUE;

    g_static_rec_mutex_init(&priv->mutex);
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

    return find_format(display->priv->image_formats, format);
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
 *
 * Returns whether VA @display supports @format subpicture format.
 *
 * Return value: %TRUE if VA @display supports @format subpicture format
 */
gboolean
gst_vaapi_display_has_subpicture_format(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);
    g_return_val_if_fail(format, FALSE);

    return find_format(display->priv->subpicture_formats, format);
}
