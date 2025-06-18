// AudioProcessor.h
#ifndef AUDIOPROCESSOR_H
#define AUDIOPROCESSOR_H

#include <QObject>
#include <QAudioInput>
#include <QAudioFormat>
#include <QIODevice>
#include <QMutex>
#include <QByteArray>
#include "Logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libavutil/time.h>
#include <libavutil/frame.h>
}
Q_DECLARE_METATYPE(AVFrame*)

class AudioProcessor : public QObject {
    Q_OBJECT
public:
    explicit AudioProcessor(QObject* parent = nullptr);
    ~AudioProcessor();

    bool initialize(int sampleRate, int channels, AVSampleFormat format);
    void startCapture();
    void stopCapture();
    void setOutputContext(AVFormatContext* fmtCtx, AVCodecContext* codecCtx, AVStream* stream);

public slots:
    void resetTimestamp();        // 重置时间戳
    int64_t getCurrentAudioPts(); // 获取当前音频PTS

signals:
    void audioFrameAvailable(AVFrame* frame);
    void errorOccurred(const QString& message);
    void audioTimestampUpdated(int64_t pts); // 音频时间戳更新信号

private:
    QAudioInput* m_audioInput = nullptr;
    QIODevice* m_audioDevice = nullptr;
    QAudioFormat m_audioFormat;

    // FFmpeg相关
    SwrContext* m_swrCtx = nullptr;
    AVFormatContext* m_outputContext = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    AVStream* m_stream = nullptr;

    // 音频参数
    int m_sampleRate;
    int m_channels;
    AVSampleFormat m_sampleFormat;

    int64_t m_pts = 0;

    // 添加音频时间戳管理
    int64_t m_audioStartTime;     // 音频开始时间戳
    bool m_isFirstFrame;          // 是否是第一帧
    QMutex m_timestampMutex;      // 时间戳同步锁

    QByteArray m_audioBuffer;

    class AudioInputDevice : public QIODevice {
    public:
        AudioInputDevice(AudioProcessor* processor) : m_owner(processor) {}

        bool isSequential() const override { return true; }
        bool open(OpenMode mode) override { return QIODevice::open(mode); }

        qint64 writeData(const char* data, qint64 len) override;
        qint64 readData(char* data, qint64 maxlen) override { Q_UNUSED(data); Q_UNUSED(maxlen); return 0; }

    private:
        AudioProcessor* m_owner;
    };

    void processAudioData(const char* data, qint64 len);
    void sendFrame(const QByteArray &frameData);
};

#endif // AUDIOPROCESSOR_H
