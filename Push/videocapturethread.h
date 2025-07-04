﻿#ifndef VIDEOCAPTURETHREAD_H
#define VIDEOCAPTURETHREAD_H

#include <QThread>
#include <QMutex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class VideoCaptureThread : public QThread
{
    Q_OBJECT
public:
    explicit VideoCaptureThread(QObject *parent = nullptr);
    ~VideoCaptureThread();

    bool initialize(const QString& sourceUrl, int width, int height, int fps);
    void stopCapture();

signals:
    void videoFrameAvailable(AVFrame* frame);
    void errorOccurred(const QString& message);

protected:
    void run() override;

private:
    AVFormatContext* m_formatCtx = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    int m_videoStreamIndex = -1;
    volatile bool m_running = false;

    QString m_sourceUrl;//视频流源地址
    int m_width = 1920;
    int m_height = 1080;
    int m_fps = 30;
};

#endif // VIDEOCAPTURETHREAD_H
