#include <QObject>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QWindow>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QQuickItem>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QAnimationDriver>

#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>

#include "qtglrenderer.h"
#include "gstqtglutility.h"

#define GST_CAT_DEFAULT gst_qt_gl_renderer_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void
init_debug (void)
{
  static volatile gsize _debug;

  if (g_once_init_enter (&_debug)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "qtglrenderer", 0,
        "Qt OpenGL Renderer");
    g_once_init_leave (&_debug, 1);
  }
}

/* Needs to be based on QWindow otherwise (at least) windows and nvidia
 * proprietary on linux does not work
 * We also need to override the size handling to get the correct output size
 */
class GstBackingSurface : public QWindow
{
public:
    GstBackingSurface();
    ~GstBackingSurface();

    void setSize (int width, int height);
    QSize size() const override;

private:
    QSize m_size;
};

GstBackingSurface::GstBackingSurface()
    : m_size(QSize())
{
    /* we do OpenGL things so need an OpenGL surface */
    setSurfaceType(QSurface::OpenGLSurface);
}

GstBackingSurface::~GstBackingSurface()
{
}

QSize GstBackingSurface::size () const
{
    return m_size;
}

void GstBackingSurface::setSize (int width, int height)
{
    m_size = QSize (width, height);
}

class GstAnimationDriver : public QAnimationDriver
{
public:
    GstAnimationDriver();

    void setNextTime(qint64 ms);
    void advance() override;
    qint64 elapsed() const override;
private:
    qint64 m_elapsed;
    qint64 m_next;
};

GstAnimationDriver::GstAnimationDriver()
    : m_elapsed(0),
      m_next(0)
{
}

void GstAnimationDriver::advance()
{
    m_elapsed = m_next;
    advanceAnimation();
}

qint64 GstAnimationDriver::elapsed() const
{
    return m_elapsed;
}

void GstAnimationDriver::setNextTime(qint64 ms)
{
    m_next = ms;
}

void
GstQuickRenderer::deactivateContext ()
{
}

void
GstQuickRenderer::activateContext ()
{
}

static void
delete_cxx (QOpenGLFramebufferObject * cxx)
{
  GST_TRACE ("freeing Qfbo %p", cxx);
  delete cxx;
}

GstQuickRenderer::GstQuickRenderer()
    : gl_context(NULL),
      m_context(nullptr),
      m_renderThread(nullptr),
      m_surface(nullptr),
      m_fbo(nullptr),
      m_quickWindow(nullptr),
      m_renderControl(nullptr),
      m_qmlEngine(nullptr),
      m_qmlComponent(nullptr),
      m_rootItem(nullptr),
      m_animationDriver(nullptr),
      gl_allocator(NULL),
      gl_params(NULL),
      gl_mem(NULL)
{
    init_debug ();
}

bool GstQuickRenderer::init (GstGLContext * context, GError ** error)
{
    g_return_val_if_fail (gst_gl_context_get_current () == context, false);

    QVariant qt_native_context = qt_opengl_native_context_from_gst_gl_context (context);

    if (qt_native_context.isNull()) {
        g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_NOT_FOUND,
            "Could not convert from the provided GstGLContext to a Qt "
            "native context");
        return false;
    }

    m_context = new QOpenGLContext;
    m_context->setNativeHandle(qt_native_context);

    m_surface = new GstBackingSurface;
    m_surface->create();  /* FIXME: may need to be called on Qt's main thread */

    m_renderThread = QThread::currentThread();
    gst_gl_context_activate (context, FALSE);

    /* Qt does some things that it may require the OpenGL context current in
     * ->create() so that it has the necessry information to create the
     * QOpenGLContext from the native handle. This may fail if the OpenGL
     * context is already current in another thread so we need to deactivate
     * the context from GStreamer's thread before asking Qt to create the
     * QOpenGLContext with ->create().
     */
    m_context->create();
    m_context->doneCurrent();

    m_context->moveToThread (m_renderThread);
    if (!m_context->makeCurrent(m_surface)) {
        g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_NOT_FOUND,
            "Could not make Qt OpenGL context current");
        /* try to keep the same OpenGL context state */
        gst_gl_context_activate (context, TRUE);
        return false;
    }

    if (!gst_gl_context_activate (context, TRUE)) {
        g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_NOT_FOUND,
            "Could not make OpenGL context current again");
        return false;
    }

    m_renderControl = new QQuickRenderControl();
    /* Create a QQuickWindow that is associated with our render control. Note that this
     * window never gets created or shown, meaning that it will never get an underlying
     * native (platform) window.
     */
    m_quickWindow = new QQuickWindow(m_renderControl);
    /* after QQuickWindow creation as QQuickRenderControl requires it */
    m_renderControl->prepareThread (m_renderThread);

    /* Create a QML engine. */
    m_qmlEngine = new QQmlEngine;
    if (!m_qmlEngine->incubationController())
        m_qmlEngine->setIncubationController(m_quickWindow->incubationController());

    gl_context = static_cast<GstGLContext*>(gst_object_ref (context));
    gl_allocator = (GstGLBaseMemoryAllocator *) gst_gl_memory_allocator_get_default (gl_context);
    gl_params = (GstGLAllocationParams *) (gst_gl_video_allocation_params_new_wrapped_texture (gl_context,
        NULL, &this->v_info, 0, NULL, GST_GL_TEXTURE_TARGET_2D, GST_GL_RGBA8,
        0, NULL, (GDestroyNotify) delete_cxx));

    return true;
}

GstQuickRenderer::~GstQuickRenderer()
{
    gst_gl_allocation_params_free (gl_params);
}

void GstQuickRenderer::stop ()
{
    g_assert (QOpenGLContext::currentContext() == m_context);

    if (m_renderControl)
        m_renderControl->invalidate();

    if (m_fbo)
        delete m_fbo;
    m_fbo = nullptr;

    m_context->doneCurrent();
    if (m_animationDriver)
        delete m_animationDriver;
    m_animationDriver = nullptr;
}

void GstQuickRenderer::cleanup()
{
    if (gl_context)
        gst_gl_context_thread_add (gl_context,
            (GstGLContextThreadFunc) GstQuickRenderer::stop_c, this);

    /* Delete the render control first since it will free the scenegraph resources.
     * Destroy the QQuickWindow only afterwards. */
    if (m_renderControl)
        delete m_renderControl;
    m_renderControl = nullptr;

    if (m_qmlComponent)
        delete m_qmlComponent;
    m_qmlComponent = nullptr;
    if (m_quickWindow)
        delete m_quickWindow;
    m_quickWindow = nullptr;
    if (m_qmlEngine)
        delete m_qmlEngine;
    m_qmlEngine = nullptr;

    gst_clear_object (&gl_context);

    if (m_context)
        delete m_context;
    m_context = nullptr;
}

void GstQuickRenderer::ensureFbo()
{
    if (m_fbo && m_fbo->size() != m_surface->size()) {
        GST_INFO ("removing old framebuffer created with size %ix%i",
            m_fbo->size().width(), m_fbo->size().height());
        delete m_fbo;
        m_fbo = nullptr;
    }

    if (!m_fbo) {
        m_fbo = new QOpenGLFramebufferObject(m_surface->size(),
                                             QOpenGLFramebufferObject::CombinedDepthStencil);
        m_quickWindow->setRenderTarget(m_fbo);
        GST_DEBUG ("new framebuffer created with size %ix%i",
            m_fbo->size().width(), m_fbo->size().height());
    }
}

void
GstQuickRenderer::renderGstGL ()
{
    GST_DEBUG ("current QOpenGLContext %p", QOpenGLContext::currentContext());
    m_quickWindow->resetOpenGLState();

    m_animationDriver->advance();

    QEventLoop loop;
    if (loop.processEvents())
        GST_LOG ("pending QEvents processed");

    ensureFbo();

    /* Synchronization and rendering happens here on the render thread. */
    if (m_renderControl->sync())
        GST_LOG ("sync successful");

    /* Meanwhile on this thread continue with the actual rendering. */
    m_renderControl->render();

    GST_DEBUG ("wrapping Qfbo %p with texture %u", m_fbo, m_fbo->texture());
    gl_params->user_data = static_cast<gpointer> (m_fbo);
    gl_params->gl_handle = GINT_TO_POINTER (m_fbo->texture());
    gl_mem = (GstGLMemory *) gst_gl_base_memory_alloc (gl_allocator, gl_params);

    m_fbo = nullptr;
}

GstGLMemory *GstQuickRenderer::generateOutput(GstClockTime input_ns)
{
    m_animationDriver->setNextTime(input_ns / GST_MSECOND);

    /* run an event loop to update any changed values for rendering */
    QEventLoop loop;
    if (loop.processEvents())
        GST_LOG ("pending QEvents processed");

    GST_LOG ("generating output for time %" GST_TIME_FORMAT " ms: %"
        G_GUINT64_FORMAT, GST_TIME_ARGS (input_ns), input_ns / GST_MSECOND);

    m_quickWindow->update();

    /* Polishing happens on the gui thread. */
    m_renderControl->polishItems();

    /* TODO: an async version could be used where */
    gst_gl_context_thread_add (gl_context, (GstGLContextThreadFunc) GstQuickRenderer::render_gst_gl_c, this);

    GstGLMemory *tmp = gl_mem;
    gl_mem = NULL;

    return tmp;
}

void GstQuickRenderer::initializeGstGL ()
{
    GST_TRACE ("current QOpenGLContext %p", QOpenGLContext::currentContext());
    if (!m_context->makeCurrent(m_surface)) {
        m_errorString = "Failed to make Qt's wrapped OpenGL context current";
        return;
    }
    GST_INFO ("current QOpenGLContext %p", QOpenGLContext::currentContext());
    m_renderControl->initialize(m_context);

    /* 1. QAnimationDriver's are thread-specific
     * 2. QAnimationDriver controls the 'animation time' that the Qml scene is
     *    rendered at
     */
    /* FIXME: what happens with multiple qmlgloverlay elements?  Do we need a 
     * shared animation driver? */
    m_animationDriver = new GstAnimationDriver;
    m_animationDriver->install();
}

void GstQuickRenderer::initializeQml()
{
    disconnect(m_qmlComponent, &QQmlComponent::statusChanged, this, &GstQuickRenderer::initializeQml);

    if (m_qmlComponent->isError()) {
        const QList<QQmlError> errorList = m_qmlComponent->errors();
        for (const QQmlError &error : errorList)
            m_errorString += error.toString();
        return;
    }

    QObject *rootObject = m_qmlComponent->create();
    if (m_qmlComponent->isError()) {
        const QList<QQmlError> errorList = m_qmlComponent->errors();
        for (const QQmlError &error : errorList)
            m_errorString += error.toString();
        delete rootObject;
        return;
    }

    m_rootItem = qobject_cast<QQuickItem *>(rootObject);
    if (!m_rootItem) {
        m_errorString += "root QML item is not a QQuickItem";
        delete rootObject;
        return;
    }

    /* The root item is ready. Associate it with the window. */
    m_rootItem->setParentItem(m_quickWindow->contentItem());

    /* Update item and rendering related geometries. */
    updateSizes();

    /* Initialize the render control and our OpenGL resources. */
    gst_gl_context_thread_add (gl_context, (GstGLContextThreadFunc) GstQuickRenderer::initialize_gst_gl_c, this);
}

void GstQuickRenderer::updateSizes()
{
    GstBackingSurface *surface = static_cast<GstBackingSurface *>(m_surface);
    /* Behave like SizeRootObjectToView. */
    QSize size = surface->size();

    m_rootItem->setWidth(size.width());
    m_rootItem->setHeight(size.height());

    m_quickWindow->setGeometry(0, 0, size.width(), size.height());

    gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, size.width(),
        size.height());
    GstGLVideoAllocationParams *params = (GstGLVideoAllocationParams *) (gl_params);
    gst_video_info_set_format (params->v_info, GST_VIDEO_FORMAT_RGBA, size.width(),
        size.height());
}

void GstQuickRenderer::setSize(int w, int h)
{
    static_cast<GstBackingSurface *>(m_surface)->setSize(w, h);
    updateSizes();
}

bool GstQuickRenderer::setQmlScene (const gchar * scene, GError ** error)
{
    /* replacing the scene is not supported */
    g_return_val_if_fail (m_qmlComponent == NULL, false);

    m_errorString = "";

    m_qmlComponent = new QQmlComponent(m_qmlEngine);
    /* XXX: do we need to provide a propper base name? */
    m_qmlComponent->setData(QByteArray (scene), QUrl(""));
    if (m_qmlComponent->isLoading())
        /* TODO: handle async properly */
        connect(m_qmlComponent, &QQmlComponent::statusChanged, this, &GstQuickRenderer::initializeQml);
    else
        initializeQml();

    if (m_errorString != "") {
        g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_SETTINGS,
            m_errorString.toUtf8());
        return FALSE;
    }

    return TRUE;
}

QQuickItem * GstQuickRenderer::rootItem() const
{
    return m_rootItem;
}
