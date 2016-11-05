# Basic Media Player

## Goal

This tutorial shows how to create a basic media player with
[Qt](http://qt-project.org/) and
[QtGStreamer](http://gstreamer.freedesktop.org/data/doc/gstreamer/head/qt-gstreamer/html/index.html).
It assumes that you are already familiar with the basics of Qt and
GStreamer. If not, please refer to the other tutorials in this
documentation.

In particular, you will learn:

  - How to create a basic pipeline
  - How to create a video output
  - Updating the GUI based on playback time

## A media player with Qt

These files are located in the qt-gstreamer SDK's `examples/` directory.

Due to the length of these samples, they are initially hidden. Click on
each file to expand.

![](images/icons/grey_arrow_down.gif)CMakeLists.txt

**CMakeLists.txt**

```
project(qtgst-example-player)
find_package(QtGStreamer REQUIRED)
## automoc is now a built-in tool since CMake 2.8.6.
if (${CMAKE_VERSION} VERSION_LESS "2.8.6")
    find_package(Automoc4 REQUIRED)
else()
    set(CMAKE_AUTOMOC TRUE)
    macro(automoc4_add_executable)
        add_executable(${ARGV})
    endmacro()
endif()
include_directories(${QTGSTREAMER_INCLUDES} ${CMAKE_CURRENT_BINARY_DIR} ${QT_QTWIDGETS_INCLUDE_DIRS})
add_definitions(${QTGSTREAMER_DEFINITIONS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${QTGSTREAMER_FLAGS}")
set(player_SOURCES main.cpp player.cpp mediaapp.cpp)
automoc4_add_executable(player ${player_SOURCES})
target_link_libraries(player ${QTGSTREAMER_UI_LIBRARIES} ${QT_QTOPENGL_LIBRARIES} ${QT_QTWIDGETS_LIBRARIES})
```

![](images/icons/grey_arrow_down.gif)main.cpp

**main.cpp**

``` c
#include "mediaapp.h"
#include <QtWidgets/QApplication>
#include <QGst/Init>
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QGst::init(&argc, &argv);
    MediaApp media;
    media.show();
    if (argc == 2) {
        media.openFile(argv[1]);
    }
    return app.exec();
}
```

![](images/icons/grey_arrow_down.gif)mediaapp.h

**mediaapp.h**

``` c
#ifndef MEDIAAPP_H
#define MEDIAAPP_H
#include <QtCore/QTimer>
#include <QtWidgets/QWidget>
#include <QtWidgets/QStyle>
class Player;
class QBoxLayout;
class QLabel;
class QSlider;
class QToolButton;
class QTimer;
class MediaApp : public QWidget
{
    Q_OBJECT
public:
    MediaApp(QWidget *parent = 0);
    ~MediaApp();
    void openFile(const QString & fileName);
private Q_SLOTS:
    void open();
    void toggleFullScreen();
    void onStateChanged();
    void onPositionChanged();
    void setPosition(int position);
    void showControls(bool show = true);
    void hideControls() { showControls(false); }
protected:
    void mouseMoveEvent(QMouseEvent *event);
private:
    QToolButton *initButton(QStyle::StandardPixmap icon, const QString & tip,
                            QObject *dstobj, const char *slot_method, QLayout *layout);
    void createUI(QBoxLayout *appLayout);
    QString m_baseDir;
    Player *m_player;
    QToolButton *m_openButton;
    QToolButton *m_fullScreenButton;
    QToolButton *m_playButton;
    QToolButton *m_pauseButton;
    QToolButton *m_stopButton;
    QSlider *m_positionSlider;
    QSlider *m_volumeSlider;
    QLabel *m_positionLabel;
    QLabel *m_volumeLabel;
    QTimer m_fullScreenTimer;
};
#endif
```

![](images/icons/grey_arrow_down.gif)mediaapp.cpp

**mediaapp.cpp**

``` c
#include "mediaapp.h"
#include "player.h"
#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#else
#include <QtGui/QBoxLayout>
#include <QtGui/QFileDialog>
#include <QtGui/QToolButton>
#include <QtGui/QLabel>
#include <QtGui/QSlider>
#include <QtGui/QMouseEvent>
#endif
MediaApp::MediaApp(QWidget *parent)
    : QWidget(parent)
{
    //create the player
    m_player = new Player(this);
    connect(m_player, SIGNAL(positionChanged()), this, SLOT(onPositionChanged()));
    connect(m_player, SIGNAL(stateChanged()), this, SLOT(onStateChanged()));
    //m_baseDir is used to remember the last directory that was used.
    //defaults to the current working directory
    m_baseDir = QLatin1String(".");
    //this timer (re-)hides the controls after a few seconds when we are in fullscreen mode
    m_fullScreenTimer.setSingleShot(true);
    connect(&m_fullScreenTimer, SIGNAL(timeout()), this, SLOT(hideControls()));
    //create the UI
    QVBoxLayout *appLayout = new QVBoxLayout;
    appLayout->setContentsMargins(0, 0, 0, 0);
    createUI(appLayout);
    setLayout(appLayout);
    onStateChanged(); //set the controls to their default state
    setWindowTitle(tr("QtGStreamer example player"));
    resize(400, 400);
}
MediaApp::~MediaApp()
{
    delete m_player;
}
void MediaApp::openFile(const QString & fileName)
{
    m_baseDir = QFileInfo(fileName).path();
    m_player->stop();
    m_player->setUri(fileName);
    m_player->play();
}
void MediaApp::open()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open a Movie"), m_baseDir);
    if (!fileName.isEmpty()) {
        openFile(fileName);
    }
}
void MediaApp::toggleFullScreen()
{
    if (isFullScreen()) {
        setMouseTracking(false);
        m_player->setMouseTracking(false);
        m_fullScreenTimer.stop();
        showControls();
        showNormal();
    } else {
        setMouseTracking(true);
        m_player->setMouseTracking(true);
        hideControls();
        showFullScreen();
    }
}
void MediaApp::onStateChanged()
{
    QGst::State newState = m_player->state();
    m_playButton->setEnabled(newState != QGst::StatePlaying);
    m_pauseButton->setEnabled(newState == QGst::StatePlaying);
    m_stopButton->setEnabled(newState != QGst::StateNull);
    m_positionSlider->setEnabled(newState != QGst::StateNull);
    m_volumeSlider->setEnabled(newState != QGst::StateNull);
    m_volumeLabel->setEnabled(newState != QGst::StateNull);
    m_volumeSlider->setValue(m_player->volume());
    //if we are in Null state, call onPositionChanged() to restore
    //the position of the slider and the text on the label
    if (newState == QGst::StateNull) {
        onPositionChanged();
    }
}
/* Called when the positionChanged() is received from the player */
void MediaApp::onPositionChanged()
{
    QTime length(0,0);
    QTime curpos(0,0);
    if (m_player->state() != QGst::StateReady &&
        m_player->state() != QGst::StateNull)
    {
        length = m_player->length();
        curpos = m_player->position();
    }
    m_positionLabel->setText(curpos.toString("hh:mm:ss.zzz")
                                        + "/" +
                             length.toString("hh:mm:ss.zzz"));
    if (length != QTime(0,0)) {
        m_positionSlider->setValue(curpos.msecsTo(QTime(0,0)) * 1000 / length.msecsTo(QTime(0,0)));
    } else {
        m_positionSlider->setValue(0);
    }
    if (curpos != QTime(0,0)) {
        m_positionLabel->setEnabled(true);
        m_positionSlider->setEnabled(true);
    }
}
/* Called when the user changes the slider's position */
void MediaApp::setPosition(int value)
{
    uint length = -m_player->length().msecsTo(QTime(0,0));
    if (length != 0 && value > 0) {
        QTime pos(0,0);
        pos = pos.addMSecs(length * (value / 1000.0));
        m_player->setPosition(pos);
    }
}
void MediaApp::showControls(bool show)
{
    m_openButton->setVisible(show);
    m_playButton->setVisible(show);
    m_pauseButton->setVisible(show);
    m_stopButton->setVisible(show);
    m_fullScreenButton->setVisible(show);
    m_positionSlider->setVisible(show);
    m_volumeSlider->setVisible(show);
    m_volumeLabel->setVisible(show);
    m_positionLabel->setVisible(show);
}
void MediaApp::mouseMoveEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (isFullScreen()) {
        showControls();
        m_fullScreenTimer.start(3000); //re-hide controls after 3s
    }
}
QToolButton *MediaApp::initButton(QStyle::StandardPixmap icon, const QString & tip,
                                  QObject *dstobj, const char *slot_method, QLayout *layout)
{
    QToolButton *button = new QToolButton;
    button->setIcon(style()->standardIcon(icon));
    button->setIconSize(QSize(36, 36));
    button->setToolTip(tip);
    connect(button, SIGNAL(clicked()), dstobj, slot_method);
    layout->addWidget(button);
    return button;
}
void MediaApp::createUI(QBoxLayout *appLayout)
{
    appLayout->addWidget(m_player);
    m_positionLabel = new QLabel();
    m_positionSlider = new QSlider(Qt::Horizontal);
    m_positionSlider->setTickPosition(QSlider::TicksBelow);
    m_positionSlider->setTickInterval(10);
    m_positionSlider->setMaximum(1000);
    connect(m_positionSlider, SIGNAL(sliderMoved(int)), this, SLOT(setPosition(int)));
    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setTickPosition(QSlider::TicksLeft);
    m_volumeSlider->setTickInterval(2);
    m_volumeSlider->setMaximum(10);
    m_volumeSlider->setMaximumSize(64,32);
    connect(m_volumeSlider, SIGNAL(sliderMoved(int)), m_player, SLOT(setVolume(int)));
    QGridLayout *posLayout = new QGridLayout;
    posLayout->addWidget(m_positionLabel, 1, 0);
    posLayout->addWidget(m_positionSlider, 1, 1, 1, 2);
    appLayout->addLayout(posLayout);
    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    m_openButton = initButton(QStyle::SP_DialogOpenButton, tr("Open File"),
                              this, SLOT(open()), btnLayout);
    m_playButton = initButton(QStyle::SP_MediaPlay, tr("Play"),
                              m_player, SLOT(play()), btnLayout);
    m_pauseButton = initButton(QStyle::SP_MediaPause, tr("Pause"),
                               m_player, SLOT(pause()), btnLayout);
    m_stopButton = initButton(QStyle::SP_MediaStop, tr("Stop"),
                              m_player, SLOT(stop()), btnLayout);
    m_fullScreenButton = initButton(QStyle::SP_TitleBarMaxButton, tr("Fullscreen"),
                                    this, SLOT(toggleFullScreen()), btnLayout);
    btnLayout->addStretch();
    m_volumeLabel = new QLabel();
    m_volumeLabel->setPixmap(
        style()->standardIcon(QStyle::SP_MediaVolume).pixmap(QSize(32, 32),
                QIcon::Normal, QIcon::On));
    btnLayout->addWidget(m_volumeLabel);
    btnLayout->addWidget(m_volumeSlider);
    appLayout->addLayout(btnLayout);
}
#include "moc_mediaapp.cpp"
```

![](images/icons/grey_arrow_down.gif)player.h

**player.h**

``` c
#ifndef PLAYER_H
#define PLAYER_H
#include <QtCore/QTimer>
#include <QtCore/QTime>
#include <QGst/Pipeline>
#include <QGst/Ui/VideoWidget>
 
class Player : public QGst::Ui::VideoWidget
{
    Q_OBJECT
public:
    Player(QWidget *parent = 0);
    ~Player();
 
    void setUri(const QString &uri);
 
    QTime position() const;
    void setPosition(const QTime &pos);
    int volume() const;
    QTime length() const;
    QGst::State state() const;
 
public Q_SLOTS:
    void play();
    void pause();
    void stop();
    void setVolume(int volume);
 
Q_SIGNALS:
    void positionChanged();
    void stateChanged();
 
private:
    void onBusMessage(const QGst::MessagePtr &message);
    void handlePipelineStateChange(const QGst::StateChangedMessagePtr &scm);
 
    QGst::PipelinePtr m_pipeline;
    QTimer m_positionTimer;
};
 
#endif //PLAYER_H
```

![](images/icons/grey_arrow_down.gif)player.cpp

**player.cpp**

``` c
#include "player.h"
#include <QtCore/QDir>
#include <QtCore/QUrl>
#include <QGlib/Connect>
#include <QGlib/Error>
#include <QGst/Pipeline>
#include <QGst/ElementFactory>
#include <QGst/Bus>
#include <QGst/Message>
#include <QGst/Query>
#include <QGst/ClockTime>
#include <QGst/Event>
#include <QGst/StreamVolume>
Player::Player(QWidget *parent)
    : QGst::Ui::VideoWidget(parent)
{
    //this timer is used to tell the ui to change its position slider & label
    //every 100 ms, but only when the pipeline is playing
    connect(&m_positionTimer, SIGNAL(timeout()), this, SIGNAL(positionChanged()));
}
Player::~Player()
{
    if (m_pipeline) {
        m_pipeline->setState(QGst::StateNull);
        stopPipelineWatch();
    }
}
void Player::setUri(const QString & uri)
{
    QString realUri = uri;
    //if uri is not a real uri, assume it is a file path
    if (realUri.indexOf("://") < 0) {
        realUri = QUrl::fromLocalFile(realUri).toEncoded();
    }
    if (!m_pipeline) {
        m_pipeline = QGst::ElementFactory::make("playbin").dynamicCast<QGst::Pipeline>();
        if (m_pipeline) {
            //let the video widget watch the pipeline for new video sinks
            watchPipeline(m_pipeline);
            //watch the bus for messages
            QGst::BusPtr bus = m_pipeline->bus();
            bus->addSignalWatch();
            QGlib::connect(bus, "message", this, &Player::onBusMessage);
        } else {
            qCritical() << "Failed to create the pipeline";
        }
    }
    if (m_pipeline) {
        m_pipeline->setProperty("uri", realUri);
    }
}
QTime Player::position() const
{
    if (m_pipeline) {
        //here we query the pipeline about its position
        //and we request that the result is returned in time format
        QGst::PositionQueryPtr query = QGst::PositionQuery::create(QGst::FormatTime);
        m_pipeline->query(query);
        return QGst::ClockTime(query->position()).toTime();
    } else {
        return QTime(0,0);
    }
}
void Player::setPosition(const QTime & pos)
{
    QGst::SeekEventPtr evt = QGst::SeekEvent::create(
        1.0, QGst::FormatTime, QGst::SeekFlagFlush,
        QGst::SeekTypeSet, QGst::ClockTime::fromTime(pos),
        QGst::SeekTypeNone, QGst::ClockTime::None
    );
    m_pipeline->sendEvent(evt);
}
int Player::volume() const
{
    if (m_pipeline) {
        QGst::StreamVolumePtr svp =
            m_pipeline.dynamicCast<QGst::StreamVolume>();
        if (svp) {
            return svp->volume(QGst::StreamVolumeFormatCubic) * 10;
        }
    }
    return 0;
}

void Player::setVolume(int volume)
{
    if (m_pipeline) {
        QGst::StreamVolumePtr svp =
            m_pipeline.dynamicCast<QGst::StreamVolume>();
        if(svp) {
            svp->setVolume((double)volume / 10, QGst::StreamVolumeFormatCubic);
        }
    }
}
QTime Player::length() const
{
    if (m_pipeline) {
        //here we query the pipeline about the content's duration
        //and we request that the result is returned in time format
        QGst::DurationQueryPtr query = QGst::DurationQuery::create(QGst::FormatTime);
        m_pipeline->query(query);
        return QGst::ClockTime(query->duration()).toTime();
    } else {
        return QTime(0,0);
    }
}
QGst::State Player::state() const
{
    return m_pipeline ? m_pipeline->currentState() : QGst::StateNull;
}
void Player::play()
{
    if (m_pipeline) {
        m_pipeline->setState(QGst::StatePlaying);
    }
}
void Player::pause()
{
    if (m_pipeline) {
        m_pipeline->setState(QGst::StatePaused);
    }
}
void Player::stop()
{
    if (m_pipeline) {
        m_pipeline->setState(QGst::StateNull);
        //once the pipeline stops, the bus is flushed so we will
        //not receive any StateChangedMessage about this.
        //so, to inform the ui, we have to emit this signal manually.
        Q_EMIT stateChanged();
    }
}
void Player::onBusMessage(const QGst::MessagePtr & message)
{
    switch (message->type()) {
    case QGst::MessageEos: //End of stream. We reached the end of the file.
        stop();
        break;
    case QGst::MessageError: //Some error occurred.
        qCritical() << message.staticCast<QGst::ErrorMessage>()->error();
        stop();
        break;
    case QGst::MessageStateChanged: //The element in message->source() has changed state
        if (message->source() == m_pipeline) {
            handlePipelineStateChange(message.staticCast<QGst::StateChangedMessage>());
        }
        break;
    default:
        break;
    }
}
void Player::handlePipelineStateChange(const QGst::StateChangedMessagePtr & scm)
{
    switch (scm->newState()) {
    case QGst::StatePlaying:
        //start the timer when the pipeline starts playing
        m_positionTimer.start(100);
        break;
    case QGst::StatePaused:
        //stop the timer when the pipeline pauses
        if(scm->oldState() == QGst::StatePlaying) {
            m_positionTimer.stop();
        }
        break;
    default:
        break;
    }
    Q_EMIT stateChanged();
}
#include "moc_player.cpp"
```

## Walkthrough

### Setting up GStreamer

We begin by looking at `main()`:

**main.cpp**

``` c
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QGst::init(&argc, &argv);
    MediaApp media;
    media.show();
    if (argc == 2) {
        media.openFile(argv[1]);
    }
    return app.exec();
}
```

We first initialize QtGStreamer by calling `QGst::init()`, passing
`argc` and `argv`. Internally, this ensures that the GLib type system
and GStreamer plugin registry is configured and initialized, along with
handling helpful environment variables such as `GST_DEBUG` and common
command line options. Please see the [Running GStreamer
Applications](http://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gst-running.html)
section of the core reference manual for details.

Construction of the `MediaApp` (derived from
[`QApplication`](http://qt-project.org/doc/qt-5.0/qtwidgets/qapplication.html))
involves constructing the `Player` object and connecting its signals to
the UI:

**MediaApp::MediaApp()**

``` c
    //create the player
    m_player = new Player(this);
    connect(m_player, SIGNAL(positionChanged()), this, SLOT(onPositionChanged()));
    connect(m_player, SIGNAL(stateChanged()), this, SLOT(onStateChanged()));
```

Next, we instruct the `MediaApp` to open the file given on the command
line, if any:

**MediaApp::openFile()**

``` c
void MediaApp::openFile(const QString & fileName)
{
    m_baseDir = QFileInfo(fileName).path();
    m_player->stop();
    m_player->setUri(fileName);
    m_player->play();
}
```

This in turn instructs the `Player` to construct our GStreamer pipeline:

**Player::setUri()**

``` c
void Player::setUri(const QString & uri)
{
    QString realUri = uri;
    //if uri is not a real uri, assume it is a file path
    if (realUri.indexOf("://") < 0) {
        realUri = QUrl::fromLocalFile(realUri).toEncoded();
    }
    if (!m_pipeline) {
        m_pipeline = QGst::ElementFactory::make("playbin").dynamicCast<QGst::Pipeline>();
        if (m_pipeline) {
            //let the video widget watch the pipeline for new video sinks
            watchPipeline(m_pipeline);
            //watch the bus for messages
            QGst::BusPtr bus = m_pipeline->bus();
            bus->addSignalWatch();
            QGlib::connect(bus, "message", this, &Player::onBusMessage);
        } else {
            qCritical() << "Failed to create the pipeline";
        }
    }
    if (m_pipeline) {
        m_pipeline->setProperty("uri", realUri);
    }
}
```

Here, we first ensure that the pipeline will receive a proper URI. If
`Player::setUri()` is called with `/home/user/some/file.mp3`, the path
is modified to `file:///home/user/some/file.mp3`. `playbin` only
accepts complete URIs.

The pipeline is created via `QGst::ElementFactory::make()`. The
`Player` object inherits from the `QGst::Ui::VideoWidget` class, which
includes a function to watch for the `prepare-xwindow-id` message, which
associates the underlying video sink with a Qt widget used for
rendering. For clarity, here is a portion of the implementation:

**prepare-xwindow-id handling**

``` c
    QGlib::connect(pipeline->bus(), "sync-message",
                  this, &PipelineWatch::onBusSyncMessage);
...
void PipelineWatch::onBusSyncMessage(const MessagePtr & msg)
{   
...
        if (msg->internalStructure()->name() == QLatin1String("prepare-xwindow-id")) {
            XOverlayPtr overlay = msg->source().dynamicCast<XOverlay>();
            m_renderer->setVideoSink(overlay);
        }
```

Once the pipeline is created, we connect to the bus' message signal (via
`QGlib::connect()`) to dispatch state change signals:

``` c
void Player::onBusMessage(const QGst::MessagePtr & message)
{
    switch (message->type()) {
    case QGst::MessageEos: //End of stream. We reached the end of the file.
        stop();
        break;
    case QGst::MessageError: //Some error occurred.
        qCritical() << message.staticCast<QGst::ErrorMessage>()->error();
        stop();
        break;
    case QGst::MessageStateChanged: //The element in message->source() has changed state
        if (message->source() == m_pipeline) {
            handlePipelineStateChange(message.staticCast<QGst::StateChangedMessage>());
        }
        break;
    default:
        break;
    }
}
void Player::handlePipelineStateChange(const QGst::StateChangedMessagePtr & scm)
{
    switch (scm->newState()) {
    case QGst::StatePlaying:
        //start the timer when the pipeline starts playing
        m_positionTimer.start(100);
        break;
    case QGst::StatePaused:
        //stop the timer when the pipeline pauses
        if(scm->oldState() == QGst::StatePlaying) {
            m_positionTimer.stop();
        }
        break;
    default:
        break;
    }
    Q_EMIT stateChanged();
}
```

Finally, we tell `playbin` what to play by setting the `uri` property:

``` c
m_pipeline->setProperty("uri", realUri);
```

### Starting Playback

After `Player::setUri()` is called, `MediaApp::openFile()` calls
`play()` on the `Player` object:

**Player::play()**

``` c
void Player::play()
{
    if (m_pipeline) {
        m_pipeline->setState(QGst::StatePlaying);
    }
}
```

The other state control methods are equally simple:

**Player state functions**

``` c
void Player::pause()
{
    if (m_pipeline) {
        m_pipeline->setState(QGst::StatePaused);
    }
}
void Player::stop()
{
    if (m_pipeline) {
        m_pipeline->setState(QGst::StateNull);
        //once the pipeline stops, the bus is flushed so we will
        //not receive any StateChangedMessage about this.
        //so, to inform the ui, we have to emit this signal manually.
        Q_EMIT stateChanged();
    }
}
```

Once the pipeline has entered the playing state, a state change message
is emitted on the GStreamer bus which gets picked up by the `Player`:

**Player::onBusMessage()**

``` c
void Player::onBusMessage(const QGst::MessagePtr & message)
{
    switch (message->type()) {
    case QGst::MessageEos: //End of stream. We reached the end of the file.
        stop();
        break;
    case QGst::MessageError: //Some error occurred.
        qCritical() << message.staticCast<QGst::ErrorMessage>()->error();
        stop();
        break;
    case QGst::MessageStateChanged: //The element in message->source() has changed state
        if (message->source() == m_pipeline) {
            handlePipelineStateChange(message.staticCast<QGst::StateChangedMessage>());
        }
        break;
    default:
        break;
    }
}
```

The `stateChanged` signal we connected to earlier is emitted and
handled:

**MediaApp::onStateChanged()**

``` c
void MediaApp::onStateChanged()
{
    QGst::State newState = m_player->state();
    m_playButton->setEnabled(newState != QGst::StatePlaying);
    m_pauseButton->setEnabled(newState == QGst::StatePlaying);
    m_stopButton->setEnabled(newState != QGst::StateNull);
    m_positionSlider->setEnabled(newState != QGst::StateNull);
    m_volumeSlider->setEnabled(newState != QGst::StateNull);
    m_volumeLabel->setEnabled(newState != QGst::StateNull);
    m_volumeSlider->setValue(m_player->volume());
    //if we are in Null state, call onPositionChanged() to restore
    //the position of the slider and the text on the label
    if (newState == QGst::StateNull) {
        onPositionChanged();
    }
}
```

This updates the UI to reflect the current state of the player's
pipeline.

Driven by a
[`QTimer`](http://qt-project.org/doc/qt-5.0/qtcore/qtimer.html), the
`Player` emits the `positionChanged` signal at regular intervals for the
UI to handle:

**MediaApp::onPositionChanged()**

``` c
void MediaApp::onPositionChanged()
{
    QTime length(0,0);
    QTime curpos(0,0);
    if (m_player->state() != QGst::StateReady &&
        m_player->state() != QGst::StateNull)
    {
        length = m_player->length();
        curpos = m_player->position();
    }
    m_positionLabel->setText(curpos.toString("hh:mm:ss.zzz")
                                        + "/" +
                             length.toString("hh:mm:ss.zzz"));
    if (length != QTime(0,0)) {
        m_positionSlider->setValue(curpos.msecsTo(QTime(0,0)) * 1000 / length.msecsTo(QTime(0,0)));
    } else {
        m_positionSlider->setValue(0);
    }
    if (curpos != QTime(0,0)) {
        m_positionLabel->setEnabled(true);
        m_positionSlider->setEnabled(true);
    }
}
```

The `MediaApp` queries the pipeline via the `Player`'s
`position()` method, which submits a position query. This is analogous
to `gst_element_query_position()`:

**Player::position()**

``` c
QTime Player::position() const
{
    if (m_pipeline) {
        //here we query the pipeline about its position
        //and we request that the result is returned in time format
        QGst::PositionQueryPtr query = QGst::PositionQuery::create(QGst::FormatTime);
        m_pipeline->query(query);
        return QGst::ClockTime(query->position()).toTime();
    } else {
        return QTime(0,0);
    }
}
```

Due to the way Qt handles signals that cross threads, there is no need
to worry about calling UI functions from outside the UI thread in this
example.

## Conclusion

This tutorial has shown:

  - How to create a basic pipeline
  - How to create a video output
  - Updating the GUI based on playback time

It has been a pleasure having you here, and see you soon\!
