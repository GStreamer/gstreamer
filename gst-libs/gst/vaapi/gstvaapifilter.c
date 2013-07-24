/*
 *  gstvaapifilter.c - Video processing abstraction
 *
 *  Copyright (C) 2013 Intel Corporation
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

#include "sysdeps.h"
#include "gstvaapifilter.h"
#include "gstvaapiutils.h"
#include "gstvaapivalue.h"
#include "gstvaapiminiobject.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapisurface_priv.h"

#if USE_VA_VPP
# include <va/va_vpp.h>
#endif

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_FILTER(obj) \
    ((GstVaapiFilter *)(obj))

typedef struct _GstVaapiFilterOpData GstVaapiFilterOpData;
struct _GstVaapiFilterOpData {
    GstVaapiFilterOp    op;
    GParamSpec         *pspec;
    volatile gint       ref_count;
    guint               va_type;
    guint               va_subtype;
    gpointer            va_caps;
    guint               va_num_caps;
    guint               va_cap_size;
    VABufferID          va_buffer;
    guint               va_buffer_size;
    guint               is_enabled      : 1;
};

struct _GstVaapiFilter {
    /*< private >*/
    GstVaapiMiniObject  parent_instance;

    GstVaapiDisplay    *display;
    VADisplay           va_display;
    VAConfigID          va_config;
    VAContextID         va_context;
    GPtrArray          *operations;
    GstVideoFormat      format;
    GArray             *formats;
    GstVaapiRectangle   crop_rect;
    guint               use_crop_rect   : 1;
};

/* ------------------------------------------------------------------------- */
/* --- VPP Helpers                                                       --- */
/* ------------------------------------------------------------------------- */

#if USE_VA_VPP
static VAProcFilterType *
vpp_get_filters_unlocked(GstVaapiFilter *filter, guint *num_filters_ptr)
{
    VAProcFilterType *filters = NULL;
    guint num_filters = 0;
    VAStatus va_status;

    num_filters = VAProcFilterCount;
    filters = g_malloc_n(num_filters, sizeof(*filters));
    if (!filters)
        goto error;

    va_status = vaQueryVideoProcFilters(filter->va_display, filter->va_context,
        filters, &num_filters);

    // Try to reallocate to the expected number of filters
    if (va_status == VA_STATUS_ERROR_MAX_NUM_EXCEEDED) {
        VAProcFilterType * const new_filters =
            g_try_realloc_n(filters, num_filters, sizeof(*new_filters));
        if (!new_filters)
            goto error;
        filters = new_filters;

        va_status = vaQueryVideoProcFilters(filter->va_display,
            filter->va_context, filters, &num_filters);
    }
    if (!vaapi_check_status(va_status, "vaQueryVideoProcFilters()"))
        goto error;

    *num_filters_ptr = num_filters;
    return filters;

error:
    g_free(filters);
    return NULL;
}

static VAProcFilterType *
vpp_get_filters(GstVaapiFilter *filter, guint *num_filters_ptr)
{
    VAProcFilterType *filters;

    GST_VAAPI_DISPLAY_LOCK(filter->display);
    filters = vpp_get_filters_unlocked(filter, num_filters_ptr);
    GST_VAAPI_DISPLAY_UNLOCK(filter->display);
    return filters;
}

static gpointer
vpp_get_filter_caps_unlocked(
    GstVaapiFilter *filter, VAProcFilterType type,
    guint cap_size, guint *num_caps_ptr)
{
    gpointer caps;
    guint num_caps = 1;
    VAStatus va_status;

    caps = g_malloc(cap_size);
    if (!caps)
        goto error;

    va_status = vaQueryVideoProcFilterCaps(filter->va_display,
        filter->va_context, type, caps, &num_caps);

    // Try to reallocate to the expected number of filters
    if (va_status == VA_STATUS_ERROR_MAX_NUM_EXCEEDED) {
        gpointer const new_caps = g_try_realloc_n(caps, num_caps, cap_size);
        if (!new_caps)
            goto error;
        caps = new_caps;

        va_status = vaQueryVideoProcFilterCaps(filter->va_display,
            filter->va_context, type, caps, &num_caps);
    }
    if (!vaapi_check_status(va_status, "vaQueryVideoProcFilterCaps()"))
        goto error;

    *num_caps_ptr = num_caps;
    return caps;

error:
    g_free(caps);
    return NULL;
}

static gpointer
vpp_get_filter_caps(GstVaapiFilter *filter, VAProcFilterType type,
    guint cap_size, guint *num_caps_ptr)
{
    gpointer caps;

    GST_VAAPI_DISPLAY_LOCK(filter->display);
    caps = vpp_get_filter_caps_unlocked(filter, type, cap_size, num_caps_ptr);
    GST_VAAPI_DISPLAY_UNLOCK(filter->display);
    return caps;
}
#endif

/* ------------------------------------------------------------------------- */
/* --- VPP Operations                                                   --- */
/* ------------------------------------------------------------------------- */

#if USE_VA_VPP
#define DEFAULT_FORMAT  GST_VIDEO_FORMAT_UNKNOWN

enum {
    PROP_0,

    PROP_FORMAT         = GST_VAAPI_FILTER_OP_FORMAT,
    PROP_CROP           = GST_VAAPI_FILTER_OP_CROP,

    N_PROPERTIES
};

static GParamSpec *g_properties[N_PROPERTIES] = { NULL, };
static gsize g_properties_initialized = FALSE;

static void
init_properties(void)
{
    /**
     * GstVaapiFilter:format:
     *
     * The forced output pixel format, expressed as a #GstVideoFormat.
     */
    g_properties[PROP_FORMAT] =
        g_param_spec_enum("format",
                          "Format",
                          "The forced output pixel format",
                          GST_TYPE_VIDEO_FORMAT,
                          DEFAULT_FORMAT,
                          G_PARAM_READWRITE);

    /**
     * GstVaapiFilter:crop-rect:
     *
     * The cropping rectangle, expressed as a #GstVaapiRectangle.
     */
    g_properties[PROP_CROP] =
        g_param_spec_boxed("crop-rect",
                           "Cropping Rectangle",
                           "The cropping rectangle",
                           GST_VAAPI_TYPE_RECTANGLE,
                           G_PARAM_READWRITE);
}

static void
ensure_properties(void)
{
    if (g_once_init_enter(&g_properties_initialized)) {
        init_properties();
        g_once_init_leave(&g_properties_initialized, TRUE);
    }
}

static void
op_data_free(GstVaapiFilterOpData *op_data)
{
    g_free(op_data->va_caps);
    g_slice_free(GstVaapiFilterOpData, op_data);
}

static inline gpointer
op_data_new(GstVaapiFilterOp op, GParamSpec *pspec)
{
    GstVaapiFilterOpData *op_data;

    op_data = g_slice_new0(GstVaapiFilterOpData);
    if (!op_data)
        return NULL;

    op_data->op         = op;
    op_data->pspec      = pspec;
    op_data->ref_count  = 1;
    op_data->va_buffer  = VA_INVALID_ID;

    switch (op) {
    case GST_VAAPI_FILTER_OP_FORMAT:
    case GST_VAAPI_FILTER_OP_CROP:
        op_data->va_type = VAProcFilterNone;
        break;
    default:
        g_assert(0 && "unsupported operation");
        goto error;
    }
    return op_data;

error:
    op_data_free(op_data);
    return NULL;
}

static inline gpointer
op_data_ref(gpointer data)
{
    GstVaapiFilterOpData * const op_data = data;

    g_return_val_if_fail(op_data != NULL, NULL);

    g_atomic_int_inc(&op_data->ref_count);
    return op_data;
}

static void
op_data_unref(gpointer data)
{
    GstVaapiFilterOpData * const op_data = data;

    g_return_if_fail(op_data != NULL);
    g_return_if_fail(op_data->ref_count > 0);

    if (g_atomic_int_dec_and_test(&op_data->ref_count))
        op_data_free(op_data);
}

/* Ensure capability info is set up for the VA filter we are interested in */
static gboolean
op_data_ensure_caps(GstVaapiFilterOpData *op_data, gpointer filter_caps,
    guint num_filter_caps)
{
    guchar *filter_cap = filter_caps;
    guint i;

    // Find the VA filter cap matching the op info sub-type
    if (op_data->va_subtype) {
        for (i = 0; i < num_filter_caps; i++) {
            /* XXX: sub-type shall always be the first field */
            if (op_data->va_subtype == *(guint *)filter_cap) {
                num_filter_caps = 1;
                break;
            }
            filter_cap += op_data->va_cap_size;
        }
        if (i == num_filter_caps)
            return FALSE;
    }
    op_data->va_caps = g_memdup(filter_cap,
        op_data->va_cap_size * num_filter_caps);
    return op_data->va_caps != NULL;
}

/* Scale the filter value wrt. library spec and VA driver spec */
static gboolean
op_data_get_value_float(GstVaapiFilterOpData *op_data,
    const VAProcFilterValueRange *range, gfloat value, gfloat *out_value_ptr)
{
    GParamSpecFloat * const pspec = G_PARAM_SPEC_FLOAT(op_data->pspec);
    gfloat out_value;

    g_return_val_if_fail(range != NULL, FALSE);
    g_return_val_if_fail(out_value_ptr != NULL, FALSE);

    if (value < pspec->minimum || value > pspec->maximum)
        return FALSE;

    // Scale wrt. the medium ("default") value
    out_value = range->default_value;
    if (value > pspec->default_value)
        out_value += ((value - pspec->default_value) /
             (pspec->maximum - pspec->default_value) *
             (range->max_value - range->default_value));
    else if (value < pspec->default_value)
        out_value -= ((pspec->default_value - value) /
             (pspec->default_value - pspec->minimum) *
             (range->default_value - range->min_value));

    *out_value_ptr = out_value;
    return TRUE;
}

/* Get default list of operations supported by the library */
static GPtrArray *
get_operations_default(void)
{
    GPtrArray *ops;
    guint i;

    ops = g_ptr_array_new_full(N_PROPERTIES, op_data_unref);
    if (!ops)
        return NULL;

    ensure_properties();

    for (i = 0; i < N_PROPERTIES; i++) {
        GParamSpec * const pspec = g_properties[i];
        if (!pspec)
            continue;

        GstVaapiFilterOpData * const op_data = op_data_new(i, pspec);
        if (!op_data)
            goto error;
        g_ptr_array_add(ops, op_data);
    }
    return ops;

error:
    g_ptr_array_unref(ops);
    return NULL;
}

/* Get the ordered list of operations, based on VA/VPP queries */
static GPtrArray *
get_operations_ordered(GstVaapiFilter *filter, GPtrArray *default_ops)
{
    GPtrArray *ops;
    VAProcFilterType *filters;
    gpointer filter_caps = NULL;
    guint i, j, num_filters, num_filter_caps = 0;

    ops = g_ptr_array_new_full(default_ops->len, op_data_unref);
    if (!ops)
        return NULL;

    filters = vpp_get_filters(filter, &num_filters);
    if (!filters)
        goto error;

    // Append virtual ops first, i.e. those without an associated VA filter
    for (i = 0; i < default_ops->len; i++) {
        GstVaapiFilterOpData * const op_data =
            g_ptr_array_index(default_ops, i);
        if (op_data->va_type == VAProcFilterNone)
            g_ptr_array_add(ops, op_data_ref(op_data));
    }

    // Append ops, while preserving the VA filters ordering
    for (i = 0; i < num_filters; i++) {
        const VAProcFilterType va_type = filters[i];
        if (va_type == VAProcFilterNone)
            continue;

        for (j = 0; j < default_ops->len; j++) {
            GstVaapiFilterOpData * const op_data =
                g_ptr_array_index(default_ops, j);
            if (op_data->va_type != va_type)
                continue;

            if (!filter_caps) {
                filter_caps = vpp_get_filter_caps(filter, va_type,
                    op_data->va_cap_size, &num_filter_caps);
                if (!filter_caps)
                    goto error;
            }
            if (!op_data_ensure_caps(op_data, filter_caps, num_filter_caps))
                goto error;
            g_ptr_array_add(ops, op_data_ref(op_data));
        }
        free(filter_caps);
        filter_caps = NULL;
    }

    if (filter->operations)
        g_ptr_array_unref(filter->operations);
    filter->operations = g_ptr_array_ref(ops);

    g_free(filters);
    g_ptr_array_unref(default_ops);
    return ops;

error:
    g_free(filter_caps);
    g_free(filters);
    g_ptr_array_unref(ops);
    g_ptr_array_unref(default_ops);
    return NULL;
}

/* Determine the set of supported VPP operations by the specific
   filter, or known to this library if filter is NULL */
static GPtrArray *
ensure_operations(GstVaapiFilter *filter)
{
    GPtrArray *ops;

    if (filter && filter->operations)
        return g_ptr_array_ref(filter->operations);

    ops = get_operations_default();
    if (!ops)
        return NULL;
    return filter ? get_operations_ordered(filter, ops) : ops;
}
#endif

/* Find whether the VPP operation is supported or not */
GstVaapiFilterOpData *
find_operation(GstVaapiFilter *filter, GstVaapiFilterOp op)
{
    guint i;

    if (!filter->operations)
        return NULL;

    for (i = 0; i < filter->operations->len; i++) {
        GstVaapiFilterOpData * const op_data =
            g_ptr_array_index(filter->operations, i);
        if (op_data->op == op)
            return op_data;
    }
    return NULL;
}

/* Ensure the operation's VA buffer is allocated */
static inline gboolean
op_ensure_buffer(GstVaapiFilter *filter, GstVaapiFilterOpData *op_data)
{
    if (G_LIKELY(op_data->va_buffer != VA_INVALID_ID))
        return TRUE;
    return vaapi_create_buffer(filter->va_display, filter->va_context,
        VAProcFilterParameterBufferType, op_data->va_buffer_size, NULL,
        &op_data->va_buffer, NULL);
}

/* ------------------------------------------------------------------------- */
/* --- Surface Formats                                                   --- */
/* ------------------------------------------------------------------------- */

static GArray *
ensure_formats(GstVaapiFilter *filter)
{
    VASurfaceAttrib *surface_attribs = NULL;
    guint i, num_surface_attribs = 0;
    VAStatus va_status;

    if (G_LIKELY(filter->formats))
        return filter->formats;

#if VA_CHECK_VERSION(0,34,0)
    GST_VAAPI_DISPLAY_LOCK(filter->display);
    va_status = vaQuerySurfaceAttributes(filter->va_display, filter->va_config,
        NULL, &num_surface_attribs);
    GST_VAAPI_DISPLAY_UNLOCK(filter->display);
    if (!vaapi_check_status(va_status, "vaQuerySurfaceAttributes()"))
        return NULL;

    surface_attribs = g_malloc(num_surface_attribs * sizeof(*surface_attribs));
    if (!surface_attribs)
        return NULL;

    GST_VAAPI_DISPLAY_LOCK(filter->display);
    va_status = vaQuerySurfaceAttributes(filter->va_display, filter->va_config,
        surface_attribs, &num_surface_attribs);
    GST_VAAPI_DISPLAY_UNLOCK(filter->display);
    if (!vaapi_check_status(va_status, "vaQuerySurfaceAttributes()"))
        return NULL;

    filter->formats = g_array_sized_new(FALSE, FALSE, sizeof(GstVideoFormat),
        num_surface_attribs);
    if (!filter->formats)
        goto error;

    for (i = 0; i < num_surface_attribs; i++) {
        const VASurfaceAttrib * const surface_attrib = &surface_attribs[i];
        GstVideoFormat format;

        if (surface_attrib->type != VASurfaceAttribPixelFormat)
            continue;
        if (!(surface_attrib->flags & VA_SURFACE_ATTRIB_SETTABLE))
            continue;

        format = gst_vaapi_video_format_from_va_fourcc(
            surface_attrib->value.value.i);
        if (format == GST_VIDEO_FORMAT_UNKNOWN)
            continue;
        g_array_append_val(filter->formats, format);
    }
#endif

    g_free(surface_attribs);
    return filter->formats;

error:
    g_free(surface_attribs);
    return NULL;
}

static inline gboolean
is_special_format(GstVideoFormat format)
{
    return format == GST_VIDEO_FORMAT_UNKNOWN ||
        format == GST_VIDEO_FORMAT_ENCODED;
}

static gboolean
find_format(GstVaapiFilter *filter, GstVideoFormat format)
{
    guint i;

    if (is_special_format(format) || !filter->formats)
        return FALSE;

    for (i = 0; i < filter->formats->len; i++) {
        if (g_array_index(filter->formats, GstVideoFormat, i) == format)
            return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------- */
/* --- Interface                                                         --- */
/* ------------------------------------------------------------------------- */

#if USE_VA_VPP
static gboolean
gst_vaapi_filter_init(GstVaapiFilter *filter, GstVaapiDisplay *display)
{
    VAStatus va_status;

    filter->display     = gst_vaapi_display_ref(display);
    filter->va_display  = GST_VAAPI_DISPLAY_VADISPLAY(display);
    filter->va_config   = VA_INVALID_ID;
    filter->va_context  = VA_INVALID_ID;
    filter->format      = DEFAULT_FORMAT;

    if (!GST_VAAPI_DISPLAY_HAS_VPP(display))
        return FALSE;

    va_status = vaCreateConfig(filter->va_display, VAProfileNone,
        VAEntrypointVideoProc, NULL, 0, &filter->va_config);
    if (!vaapi_check_status(va_status, "vaCreateConfig() [VPP]"))
        return FALSE;

    va_status = vaCreateContext(filter->va_display, filter->va_config, 0, 0, 0,
        NULL, 0, &filter->va_context);
    if (!vaapi_check_status(va_status, "vaCreateContext() [VPP]"))
        return FALSE;
    return TRUE;
}

static void
gst_vaapi_filter_finalize(GstVaapiFilter *filter)
{
    guint i;

    GST_VAAPI_DISPLAY_LOCK(filter->display);
    if (filter->operations) {
        for (i = 0; i < filter->operations->len; i++) {
            GstVaapiFilterOpData * const op_data =
                g_ptr_array_index(filter->operations, i);
            vaapi_destroy_buffer(filter->va_display, &op_data->va_buffer);
        }
        g_ptr_array_unref(filter->operations);
        filter->operations = NULL;
    }

    if (filter->va_context != VA_INVALID_ID) {
        vaDestroyContext(filter->va_display, filter->va_context);
        filter->va_context = VA_INVALID_ID;
    }

    if (filter->va_config != VA_INVALID_ID) {
        vaDestroyConfig(filter->va_display, filter->va_config);
        filter->va_config = VA_INVALID_ID;
    }
    GST_VAAPI_DISPLAY_UNLOCK(filter->display);
    gst_vaapi_display_replace(&filter->display, NULL);

    if (filter->formats) {
        g_array_unref(filter->formats);
        filter->formats = NULL;
    }
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_filter_class(void)
{
    static const GstVaapiMiniObjectClass GstVaapiFilterClass = {
        sizeof(GstVaapiFilter),
        (GDestroyNotify)gst_vaapi_filter_finalize
    };
    return &GstVaapiFilterClass;
}
#endif

/**
 * gst_vaapi_filter_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiFilter set up to operate in "identity"
 * mode. This means that no other operation than scaling is performed.
 *
 * Return value: the newly created #GstVaapiFilter object
 */
GstVaapiFilter *
gst_vaapi_filter_new(GstVaapiDisplay *display)
{
#if USE_VA_VPP
    GstVaapiFilter *filter;

    filter = (GstVaapiFilter *)
        gst_vaapi_mini_object_new0(gst_vaapi_filter_class());
    if (!filter)
        return NULL;

    if (!gst_vaapi_filter_init(filter, display))
        goto error;
    return filter;

error:
    gst_vaapi_filter_unref(filter);
    return NULL;
#else
    GST_WARNING("video processing is not supported, "
                "please consider an upgrade to VA-API >= 0.34");
    return NULL;
#endif
}

/**
 * gst_vaapi_filter_ref:
 * @filter: a #GstVaapiFilter
 *
 * Atomically increases the reference count of the given @filter by one.
 *
 * Returns: The same @filter argument
 */
GstVaapiFilter *
gst_vaapi_filter_ref(GstVaapiFilter *filter)
{
    g_return_val_if_fail(filter != NULL, NULL);

    return GST_VAAPI_FILTER(gst_vaapi_mini_object_ref(
                                GST_VAAPI_MINI_OBJECT(filter)));
}

/**
 * gst_vaapi_filter_unref:
 * @filter: a #GstVaapiFilter
 *
 * Atomically decreases the reference count of the @filter by one. If
 * the reference count reaches zero, the filter will be free'd.
 */
void
gst_vaapi_filter_unref(GstVaapiFilter *filter)
{
    g_return_if_fail(filter != NULL);

    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(filter));
}

/**
 * gst_vaapi_filter_replace:
 * @old_filter_ptr: a pointer to a #GstVaapiFilter
 * @new_filter: a #GstVaapiFilter
 *
 * Atomically replaces the filter held in @old_filter_ptr with
 * @new_filter. This means that @old_filter_ptr shall reference a
 * valid filter. However, @new_filter can be NULL.
 */
void
gst_vaapi_filter_replace(GstVaapiFilter **old_filter_ptr,
    GstVaapiFilter *new_filter)
{
    g_return_if_fail(old_filter_ptr != NULL);

    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)old_filter_ptr,
        GST_VAAPI_MINI_OBJECT(new_filter));
}

/**
 * gst_vaapi_filter_get_operations:
 * @filter: a #GstVaapiFilter, or %NULL
 *
 * Determines the set of supported operations for video processing.
 * The caller owns an extra reference to the resulting array of
 * #GstVaapiFilterOpInfo elements, so it shall be released with
 * g_ptr_array_unref() after usage.
 *
 * If @filter is %NULL, then this function returns the video
 * processing operations supported by this library.
 *
 * Return value: the set of supported operations, or %NULL if an error
 *   occurred.
 */
GPtrArray *
gst_vaapi_filter_get_operations(GstVaapiFilter *filter)
{
#if USE_VA_VPP
    return ensure_operations(filter);
#else
    return NULL;
#endif
}

/**
 * gst_vaapi_filter_set_operation:
 * @filter: a #GstVaapiFilter
 * @op: a #GstVaapiFilterOp
 * @value: the @op settings
 *
 * Enable the specified operation @op to be performed during video
 * processing, i.e. in gst_vaapi_filter_process(). The @value argument
 * specifies the operation settings. e.g. deinterlacing method for
 * deinterlacing, denoising level for noise reduction, etc.
 *
 * If @value is %NULL, then this function resets the operation
 * settings to their default values.
 *
 * Return value: %TRUE if the specified operation may be supported,
 *   %FALSE otherwise
 */
gboolean
gst_vaapi_filter_set_operation(GstVaapiFilter *filter, GstVaapiFilterOp op,
    const GValue *value)
{
#if USE_VA_VPP
    GstVaapiFilterOpData *op_data;

    g_return_val_if_fail(filter != NULL, FALSE);

    op_data = find_operation(filter, op);
    if (!op_data)
        return FALSE;

    if (value && !G_VALUE_HOLDS(value, G_PARAM_SPEC_VALUE_TYPE(op_data->pspec)))
        return FALSE;

    switch (op) {
    case GST_VAAPI_FILTER_OP_FORMAT:
        return gst_vaapi_filter_set_format(filter, value ?
            g_value_get_enum(value) : DEFAULT_FORMAT);
    case GST_VAAPI_FILTER_OP_CROP:
        return gst_vaapi_filter_set_cropping_rectangle(filter, value ?
            g_value_get_boxed(value) : NULL);
    default:
        break;
    }
#endif
    return FALSE;
}

/**
 * gst_vaapi_filter_process:
 * @filter: a #GstVaapiFilter
 * @src_surface: the source @GstVaapiSurface
 * @dst_surface: the destination @GstVaapiSurface
 * @flags: #GstVaapiSurfaceRenderFlags that apply to @src_surface
 *
 * Applies the operations currently defined in the @filter to
 * @src_surface and return the output in @dst_surface. The order of
 * operations is determined in a way that suits best the underlying
 * hardware. i.e. the only guarantee held is the generated outcome,
 * not any specific order of operations.
 *
 * Return value: a #GstVaapiFilterStatus
 */
static GstVaapiFilterStatus
gst_vaapi_filter_process_unlocked(GstVaapiFilter *filter,
    GstVaapiSurface *src_surface, GstVaapiSurface *dst_surface, guint flags)
{
#if USE_VA_VPP
    VAProcPipelineParameterBuffer *pipeline_param = NULL;
    VABufferID pipeline_param_buf_id;
    VABufferID filters[N_PROPERTIES];
    guint i, num_filters = 0;
    VAStatus va_status;
    VARectangle src_rect, dst_rect;

    if (!ensure_operations(filter))
        return GST_VAAPI_FILTER_STATUS_ERROR_ALLOCATION_FAILED;

    if (filter->use_crop_rect) {
        const GstVaapiRectangle * const crop_rect = &filter->crop_rect;

        if ((crop_rect->x + crop_rect->width >
             GST_VAAPI_SURFACE_WIDTH(src_surface)) ||
            (crop_rect->y + crop_rect->height >
             GST_VAAPI_SURFACE_HEIGHT(src_surface)))
            goto error;

        src_rect.x      = crop_rect->x;
        src_rect.y      = crop_rect->y;
        src_rect.width  = crop_rect->width;
        src_rect.height = crop_rect->height;
    }
    else {
        src_rect.x      = 0;
        src_rect.y      = 0;
        src_rect.width  = GST_VAAPI_SURFACE_WIDTH(src_surface);
        src_rect.height = GST_VAAPI_SURFACE_HEIGHT(src_surface);
    }

    dst_rect.x      = 0;
    dst_rect.y      = 0;
    dst_rect.width  = GST_VAAPI_SURFACE_WIDTH(dst_surface);
    dst_rect.height = GST_VAAPI_SURFACE_HEIGHT(dst_surface);

    for (i = 0, num_filters = 0; i < filter->operations->len; i++) {
        GstVaapiFilterOpData * const op_data =
            g_ptr_array_index(filter->operations, i);
        if (!op_data->is_enabled)
            continue;
        if (op_data->va_buffer == VA_INVALID_ID) {
            GST_ERROR("invalid VA buffer for operation %s",
                      g_param_spec_get_name(op_data->pspec));
            goto error;
        }
        filters[num_filters++] = op_data->va_buffer;
    }

    if (!vaapi_create_buffer(filter->va_display, filter->va_context,
            VAProcPipelineParameterBufferType, sizeof(*pipeline_param),
            NULL, &pipeline_param_buf_id, (gpointer *)&pipeline_param))
        goto error;

    memset(pipeline_param, 0, sizeof(*pipeline_param));
    pipeline_param->surface = GST_VAAPI_OBJECT_ID(src_surface);
    pipeline_param->surface_region = &src_rect;
    pipeline_param->surface_color_standard = VAProcColorStandardNone;
    pipeline_param->output_region = &dst_rect;
    pipeline_param->output_color_standard = VAProcColorStandardNone;
    pipeline_param->output_background_color = 0xff000000;
    pipeline_param->filter_flags = from_GstVaapiSurfaceRenderFlags(flags);
    pipeline_param->filters = filters;
    pipeline_param->num_filters = num_filters;

    vaapi_unmap_buffer(filter->va_display, pipeline_param_buf_id, NULL);

    va_status = vaBeginPicture(filter->va_display, filter->va_context,
        GST_VAAPI_OBJECT_ID(dst_surface));
    if (!vaapi_check_status(va_status, "vaBeginPicture()"))
        goto error;

    va_status = vaRenderPicture(filter->va_display, filter->va_context,
        &pipeline_param_buf_id, 1);
    if (!vaapi_check_status(va_status, "vaRenderPicture()"))
        goto error;

    va_status = vaEndPicture(filter->va_display, filter->va_context);
    if (!vaapi_check_status(va_status, "vaEndPicture()"))
        goto error;
    return GST_VAAPI_FILTER_STATUS_SUCCESS;

error:
    vaDestroyBuffer(filter->va_display, pipeline_param_buf_id);
    return GST_VAAPI_FILTER_STATUS_ERROR_OPERATION_FAILED;
#endif
    return GST_VAAPI_FILTER_STATUS_ERROR_UNSUPPORTED_OPERATION;
}

GstVaapiFilterStatus
gst_vaapi_filter_process(GstVaapiFilter *filter, GstVaapiSurface *src_surface,
    GstVaapiSurface *dst_surface, guint flags)
{
    GstVaapiFilterStatus status;

    g_return_val_if_fail(filter != NULL,
        GST_VAAPI_FILTER_STATUS_ERROR_INVALID_PARAMETER);
    g_return_val_if_fail(src_surface != NULL,
        GST_VAAPI_FILTER_STATUS_ERROR_INVALID_PARAMETER);
    g_return_val_if_fail(dst_surface != NULL,
        GST_VAAPI_FILTER_STATUS_ERROR_INVALID_PARAMETER);

    GST_VAAPI_DISPLAY_LOCK(filter->display);
    status = gst_vaapi_filter_process_unlocked(filter,
        src_surface, dst_surface, flags);
    GST_VAAPI_DISPLAY_UNLOCK(filter->display);
    return status;
}

/**
 * gst_vaapi_filter_get_formats:
 * @filter: a #GstVaapiFilter
 *
 * Determines the set of supported source or target formats for video
 * processing.  The caller owns an extra reference to the resulting
 * array of #GstVideoFormat elements, so it shall be released with
 * g_array_unref() after usage.
 *
 * Return value: the set of supported target formats for video processing.
 */
GArray *
gst_vaapi_filter_get_formats(GstVaapiFilter *filter)
{
    g_return_val_if_fail(filter != NULL, NULL);

    return ensure_formats(filter);
}

/**
 * gst_vaapi_filter_set_format:
 * @filter: a #GstVaapiFilter
 * @format: the target surface format
 *
 * Sets the desired pixel format of the resulting video processing
 * operations.
 *
 * If @format is #GST_VIDEO_FORMAT_UNKNOWN, the filter will assume iso
 * format conversion, i.e. no color conversion at all and the target
 * surface format shall match the source surface format.
 *
 * If @format is #GST_VIDEO_FORMAT_ENCODED, the filter will use the pixel
 * format of the target surface passed to gst_vaapi_filter_process().
 *
 * Return value: %TRUE if the color conversion to the specified @format
 *   may be supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_format(GstVaapiFilter *filter, GstVideoFormat format)
{
    g_return_val_if_fail(filter != NULL, FALSE);

    if (!ensure_formats(filter))
        return FALSE;

    if (!is_special_format(format) && !find_format(filter, format))
        return FALSE;

    filter->format = format;
    return TRUE;
}

/**
 * gst_vaapi_filter_set_cropping_rectangle:
 * @filter: a #GstVaapiFilter
 * @rect: the cropping region
 *
 * Sets the source surface cropping rectangle to use during the video
 * processing. If @rect is %NULL, the whole source surface will be used.
 *
 * Return value: %TRUE if the operation is supported, %FALSE otherwise.
 */
gboolean
gst_vaapi_filter_set_cropping_rectangle(GstVaapiFilter *filter,
    const GstVaapiRectangle *rect)
{
    g_return_val_if_fail(filter != NULL, FALSE);

    filter->use_crop_rect = rect != NULL;
    if (filter->use_crop_rect)
        filter->crop_rect = *rect;
    return TRUE;
}
