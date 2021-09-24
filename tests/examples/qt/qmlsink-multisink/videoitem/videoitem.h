#ifndef VIDEOITEM_H
#define VIDEOITEM_H

#include <QQuickItem>

struct VideoItemPrivate;

class VideoItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QRect rect READ rect NOTIFY rectChanged)
    Q_PROPERTY(QSize resolution READ resolution NOTIFY resolutionChanged)

public:
    enum State {
        STATE_VOID_PENDING = 0,
        STATE_NULL = 1,
        STATE_READY = 2,
        STATE_PAUSED = 3,
        STATE_PLAYING = 4
    };
    Q_ENUM(State);

    explicit VideoItem(QQuickItem *parent = nullptr);
    ~VideoItem();

    bool hasVideo() const;

    QString source() const;
    void setSource(const QString &source);

    State state() const;
    void setState(State state);

    QRect rect() const;

    QSize resolution() const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();

signals:
    void hasVideoChanged(bool hasVideo);
    void stateChanged(VideoItem::State state);
    void sourceChanged(const QString &source);
    void rectChanged(const QRect &rect);
    void resolutionChanged(const QSize &resolution);

    void errorOccurred(const QString &error);

protected:
    void componentComplete() override;
    void releaseResources() override;

private:
    void updateRect();
    void setRect(const QRect &rect);
    void setResolution(const QSize &resolution);

private:
    QSharedPointer<VideoItemPrivate> _priv;
};

#endif // VIDEOITEM_H
