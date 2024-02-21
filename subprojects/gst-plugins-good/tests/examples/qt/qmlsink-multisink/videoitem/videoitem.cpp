#include <QQuickWindow>
#include <QQmlEngine>
#include <QRunnable>

#include <gst/gst.h>
#include <gst/gl/gl.h>

#include "videoitem.h"

static void registerMetatypes()
{
    qmlRegisterType<VideoItem>("ACME.VideoItem", 1, 0, "VideoItem");
    qRegisterMetaType<VideoItem::State>("VideoItem::State");
}
Q_CONSTRUCTOR_FUNCTION(registerMetatypes)

static const QStringList patterns = {
    "smpte",
    "snow",
    "black",
    "white",
    "red",
    "green",
    "blue",
    "checkers-1",
    "checkers-2",
    "checkers-4",
    "checkers-8",
    "circular",
    "blink",
    "smpte75",
    "zone-plate",
    "gamut",
    "chroma-zone-plate",
    "solid-color",
    "ball",
    "smpte100",
    "bar",
    "pinwheel",
    "spokes",
    "gradient",
    "colors"
};

struct VideoItemPrivate {
    explicit VideoItemPrivate(VideoItem *owner) : own(owner) { }

    VideoItem *own { nullptr };

    GstElement *pipeline { nullptr };
    GstElement *src { nullptr };
    GstElement *sink { nullptr };

    GstPad *renderPad { nullptr };
    GstBus *bus { nullptr };

    VideoItem::State state { VideoItem::STATE_VOID_PENDING };

    QString pattern {};
    QRect rect { 0, 0, 0, 0 };
    QSize resolution { 0, 0 };

    /* TODO: make q-properties? */
    quint64 timeout { 3000 }; /* 3s timeout */
};

struct RenderJob : public QRunnable {
    using Callable = std::function<void()>;

    explicit RenderJob(Callable c) : _c(c) { }

    void run() { _c(); }

private:
    Callable _c;
};

namespace {

GstBusSyncReply messageHandler(GstBus * /*bus*/, GstMessage *msg, gpointer userData)
{
    auto priv = static_cast<VideoItemPrivate *>(userData);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError *error { nullptr };
        QString str { "GStreamer error: " };

        gst_message_parse_error(msg, &error, nullptr);
        str.append(error->message);
        g_error_free(error);

        emit priv->own->errorOccurred(str);
        qWarning() << str;
    } break;

    case GST_MESSAGE_STATE_CHANGED: {
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(priv->pipeline)) {
            GstState oldState { GST_STATE_NULL }, newState { GST_STATE_NULL };

            gst_message_parse_state_changed(msg, &oldState, &newState, nullptr);
            priv->own->setState(static_cast<VideoItem::State>(newState));
        }
    } break;

    case GST_MESSAGE_HAVE_CONTEXT: {
        GstContext *context { nullptr };

        gst_message_parse_have_context(msg, &context);

        if (gst_context_has_context_type(context, GST_GL_DISPLAY_CONTEXT_TYPE))
            gst_element_set_context(priv->pipeline, context);

        if (context)
            gst_context_unref(context);

        gst_message_unref(msg);

        return GST_BUS_DROP;
    } break;

    default:
        break;
    }

    return GST_BUS_PASS;
}

} // end namespace

VideoItem::VideoItem(QQuickItem *parent)
    : QQuickItem(parent), _priv(new VideoItemPrivate(this))
{
    connect(this, &VideoItem::rectChanged, this, &VideoItem::updateRect);
    connect(this, &VideoItem::stateChanged, this, &VideoItem::updateRect);

    // gst init pipeline
    _priv->pipeline = gst_pipeline_new(nullptr);
    _priv->src = gst_element_factory_make("videotestsrc", nullptr);
    _priv->sink = gst_element_factory_make("glsinkbin", nullptr);

    GstElement *fakesink = gst_element_factory_make("fakesink", nullptr);

    Q_ASSERT(_priv->pipeline && _priv->src && _priv->sink);
    Q_ASSERT(fakesink);

    g_object_set(_priv->sink, "sink", fakesink, nullptr);

    gst_bin_add_many(GST_BIN(_priv->pipeline), _priv->src, _priv->sink, nullptr);
    gst_element_link_many (_priv->src, _priv->sink, nullptr);

    // add watch
    _priv->bus = gst_pipeline_get_bus(GST_PIPELINE(_priv->pipeline));
    gst_bus_set_sync_handler(_priv->bus, messageHandler, _priv.data(), nullptr);

    gst_element_set_state(_priv->pipeline, GST_STATE_READY);
    gst_element_get_state(_priv->pipeline, nullptr, nullptr, _priv->timeout * GST_MSECOND);
}

VideoItem::~VideoItem()
{
    gst_bus_set_sync_handler(_priv->bus, nullptr, nullptr, nullptr); // stop handling messages

    gst_element_set_state(_priv->pipeline, GST_STATE_NULL);

    gst_object_unref(_priv->pipeline);
    gst_object_unref(_priv->bus);
}

bool VideoItem::hasVideo() const
{
    return _priv->renderPad && (state() & STATE_PLAYING);
}

QString VideoItem::source() const
{
    return _priv->pattern;
}

void VideoItem::setSource(const QString &source)
{
    if (_priv->pattern == source)
        return;

    _priv->pattern = source;

    stop();

    if (!_priv->pattern.isEmpty()) {

        auto it = std::find (patterns.begin(), patterns.end(), source);

        if (it != patterns.end()) {
            g_object_set(_priv->src, "pattern", std::distance(patterns.begin(), it), nullptr);
            play();
        }
    }
    emit sourceChanged(_priv->pattern);
}

void VideoItem::play()
{
    if (_priv->state > STATE_NULL) {
        const auto status = gst_element_set_state(_priv->pipeline, GST_STATE_PLAYING);

        if (status == GST_STATE_CHANGE_FAILURE)
            qWarning() << "GStreamer error: unable to start playback";
    }
}

void VideoItem::stop()
{
    if (_priv->state > STATE_NULL) {
        const auto status = gst_element_set_state(_priv->pipeline, GST_STATE_READY);

        if (status == GST_STATE_CHANGE_FAILURE)
            qWarning() << "GStreamer error: unable to stop playback";
    }
}

void VideoItem::componentComplete()
{
    QQuickItem::componentComplete();

    QQuickItem *videoItem = findChild<QQuickItem *>("videoItem");
    Q_ASSERT(videoItem); // should not fail: check VideoItem.qml

    // needed for proper OpenGL context setup for GStreamer elements (QtQuick renderer)
    auto setRenderer = [=](QQuickWindow *window) {
        if (window) {
            GstElement *glsink = gst_element_factory_make("qmlglsink", nullptr);
            Q_ASSERT(glsink);
            gst_object_ref_sink (glsink);

            GstState current {GST_STATE_NULL}, pending {GST_STATE_NULL}, target {GST_STATE_NULL};
            auto status = gst_element_get_state(_priv->pipeline, &current, &pending, 0);

            switch (status) {
            case GST_STATE_CHANGE_FAILURE: {
                qWarning() << "GStreamer error: while setting renderer: pending state change failure";
                return;
            }
            case GST_STATE_CHANGE_SUCCESS:
                Q_FALLTHROUGH();
            case GST_STATE_CHANGE_NO_PREROLL: {
                target = current;
                break;
            }
            case GST_STATE_CHANGE_ASYNC: {
                target = pending;
                break;
            }
            }

            gst_element_set_state(_priv->pipeline, GST_STATE_NULL);

            window->scheduleRenderJob(new RenderJob([=] {
                g_object_set(glsink, "widget", videoItem, nullptr);
                _priv->renderPad = gst_element_get_static_pad(glsink, "sink");
                g_object_set(_priv->sink, "sink", glsink, nullptr);
                gst_element_set_state(_priv->pipeline, target);
                gst_object_unref (glsink);
                }),
                QQuickWindow::BeforeSynchronizingStage);
        }
    };

    setRenderer(window());
    connect(this, &QQuickItem::windowChanged, this, setRenderer);
}

void VideoItem::releaseResources()
{
    GstElement *sink { nullptr };

    gst_element_set_state(_priv->pipeline, GST_STATE_NULL);
    g_object_get(_priv->sink, "sink", &sink, nullptr);

    if (sink && _priv->renderPad) {
        g_object_set(sink, "widget", nullptr, nullptr);
    }

    gst_clear_object (&_priv->renderPad);
    gst_clear_object (&sink);
}

void VideoItem::updateRect()
{
    // WARNING: don't touch this
    if (!_priv->renderPad || _priv->state < STATE_PLAYING) {
        setRect(QRect(0, 0, 0, 0));
        setResolution(QSize(0, 0));
        return;
    }

    // update size
    GstCaps *caps = gst_pad_get_current_caps(_priv->renderPad);
    GstStructure *caps_struct = gst_caps_get_structure(caps, 0);

    gint picWidth { 0 }, picHeight { 0 };
    gst_structure_get_int(caps_struct, "width", &picWidth);
    gst_structure_get_int(caps_struct, "height", &picHeight);

    qreal winWidth { this->width() }, winHeight { this->height() };

    float picScaleRatio = picWidth * 1.0f / picHeight;
    float wndScaleRatio = winWidth / winHeight;

    if (picScaleRatio >= wndScaleRatio) {
        float span = winHeight - winWidth * picHeight / picWidth;
        setRect(QRect(0, span / 2, winWidth, winHeight - span));
    } else {
        float span = winWidth - winHeight * picWidth / picHeight;
        setRect(QRect(span / 2, 0, winWidth - span, winHeight));
    }
    setResolution(QSize(picWidth, picHeight));
    gst_clear_caps(&caps);
}

VideoItem::State VideoItem::state() const
{
    return _priv->state;
}

void VideoItem::setState(VideoItem::State state)
{
    if (_priv->state == state)
        return;

    _priv->state = state;

    emit hasVideoChanged(_priv->state & STATE_PLAYING);
    emit stateChanged(_priv->state);
}

QRect VideoItem::rect() const
{
    return _priv->rect;
}

void VideoItem::setRect(const QRect &rect)
{
    if (_priv->rect == rect)
        return;

    _priv->rect = rect;
    emit rectChanged(_priv->rect);
}

QSize VideoItem::resolution() const
{
    return _priv->resolution;
}

void VideoItem::setResolution(const QSize &size)
{
    if (_priv->resolution == size)
        return;

    _priv->resolution = size;
    emit resolutionChanged(_priv->resolution);
}
