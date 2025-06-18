#ifndef AUDIOCAPTURETHREAD_H
#define AUDIOCAPTURETHREAD_H

#include <QThread>
#include <QAudioInput>
#include <QIODevice>
#include <QAudioDeviceInfo> // 新增

class AudioCaptureThread : public QThread
{
    Q_OBJECT
public:
    explicit AudioCaptureThread(QObject* parent = nullptr);
    ~AudioCaptureThread();

    bool initialize(int sampleRate, int channels);
    void stopCapture();

signals:
    void audioDataAvailable(const QByteArray& data);

protected:
    void run() override;

private:
    QAudioInput* m_audioInput = nullptr;
    QIODevice* m_audioDevice = nullptr;
    QAudioFormat m_audioFormat;
    volatile bool m_running = false;

    class AudioInputDevice : public QIODevice {
    public:
        explicit AudioInputDevice(AudioCaptureThread* owner) : m_owner(owner) {}
        qint64 readData(char*, qint64) override { return 0; }
        qint64 writeData(const char* data, qint64 len) override;
    private:
        AudioCaptureThread* m_owner;
    };
};

#endif // AUDIOCAPTURETHREAD_H
