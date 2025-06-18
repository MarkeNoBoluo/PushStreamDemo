// CodeThread.cpp
#include "codethread.h"
#include <memory>
#include "Logger.h"



CodeThread::CodeThread(QObject* parent)
    : QThread(parent)
{
    avformat_network_init();//初始化网络
    avdevice_register_all();//初始化ffmpeg
#ifdef  Q_OS_WIN
    mInputFormat = av_find_input_format("gdigrab");//获取GDI屏幕录制设备
#else
    mInputFormat = av_find_input_format("x11grab");//获取X11屏幕录制设备
#endif
    if(!mInputFormat){
        LogErr<< "【编码器】无法打开屏幕录制设备";
    }
}

CodeThread::~CodeThread()
{
    stop();
    wait();
    cleanup();
}

void CodeThread::setDestinationUrl(const QString &url)
{
    mDstUrl = url;
    inferOutputFormat();
}

void CodeThread::stop()
{
    QMutexLocker locker(&mMutex);
    mRunning = false;
    emit stateChanged(PushState::end);
}

void CodeThread::run()
{
    cleanup();  // 确保停止后资源释放
    mRunning = true;
    mErrorCount = 0;

    if (!initializeSource()) {
        emit error("【编码器】初始化当前源地址失败");
        emit stateChanged(PushState::error);
        return;
    }

    if (!initializeDestination()) {
        emit error("【编码器】初始化目的地址失败");
        emit stateChanged(PushState::error);
        return;
    }

    emit stateChanged(PushState::play);

    // 分配帧缓冲
    AVFrame* srcFrame = av_frame_alloc();
    AVFrame* dstFrame = av_frame_alloc();
    if (!srcFrame || !dstFrame) {
        emit error("【编码器】分配帧缓冲失败");
        emit stateChanged(PushState::error);
        return;
    }

    // 设置目标帧参数
    dstFrame->format = mDstVideoCodecCtx->pix_fmt;
    dstFrame->width = mDstVideoWidth;
    dstFrame->height = mDstVideoHeight;

    // 分配帧缓冲内存
    int dstFrameSize = av_image_get_buffer_size(
        mDstVideoCodecCtx->pix_fmt,
        mDstVideoWidth,
        mDstVideoHeight,
        1
        );
    auto frameBuffer = std::make_unique<uint8_t[]>(dstFrameSize);

    av_image_fill_arrays(
        dstFrame->data,
        dstFrame->linesize,
        frameBuffer.get(),
        mDstVideoCodecCtx->pix_fmt,
        mDstVideoWidth,
        mDstVideoHeight,
        1
        );

    // 创建图像转换上下文
    SwsContext* swsCtx = sws_getContext(
        mSrcVideoWidth, mSrcVideoHeight, mSrcVideoCodecCtx->pix_fmt,
        mDstVideoWidth, mDstVideoHeight, mDstVideoCodecCtx->pix_fmt,
        SWS_BICUBIC, nullptr, nullptr, nullptr
        );

    qint64 frameCount = 0;

    while (mRunning) {
        // 处理音视频同步
        synchronizeFrames();

        if (!processNextFrame(srcFrame, dstFrame, swsCtx)) {
            if (++mErrorCount >= MAX_ERROR_COUNT) {
                emit error(QString("【编码器】连续%1个视频帧编码失败,推流中断").arg(MAX_ERROR_COUNT));
                break;
            }
        } else {
            mErrorCount = 0;
            emit frameProcessed(++frameCount);
        }
    }

    avformat_close_input(&mSrcFmtCtx);
    avformat_close_input(&mDstFmtCtx);
    // 清理
    av_frame_free(&srcFrame);
    av_frame_free(&dstFrame);
    sws_freeContext(swsCtx);

    emit stateChanged(PushState::end);
}

bool CodeThread::initializeSource()
{
    emit stateChanged(PushState::decode);
    mSrcFmtCtx = avformat_alloc_context(); // 初始化输入上下文
    if (!mSrcFmtCtx) {
        handleFFmpegError(-1,"无法分配输入上下文内存");
        return false;
    }

    AVDictionary* fmt_options = NULL; // 初始化输入上下文字典参数
    av_dict_set(&fmt_options, "framerate", QString::number(mDstVideoFps).toStdString().c_str(), 0);
    av_dict_set(&fmt_options, "draw_mouse", "1", 0);
    av_dict_set(&fmt_options, "video_size",
                QString("%1x%2").arg(mDstVideoWidth).arg(mDstVideoHeight).toStdString().c_str(), 0);

    // 打开输入封装上下文
    int ret = avformat_open_input(&mSrcFmtCtx, mSrcUrl.toLocal8Bit().data(), mInputFormat, &fmt_options);
    if (!handleFFmpegError(ret, "打开输入源")) {
        return false;
    }

    // 获取流信息
    ret = avformat_find_stream_info(mSrcFmtCtx, NULL);
    if (!handleFFmpegError(ret, "获取流信息")) {
        return false;
    }

    // 查找最佳视频流
    mSrcVideoIndex = av_find_best_stream(mSrcFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (!handleFFmpegError(mSrcVideoIndex, "查找视频流")) {
        return false;
    }

    mSrcVideoStream = mSrcFmtCtx->streams[mSrcVideoIndex];
    AVCodec* codec = avcodec_find_decoder(mSrcVideoStream->codecpar->codec_id);
    if (!codec) {
        handleFFmpegError(-1,"找不到合适的解码器");
        return false;
    }

    // 创建解码器上下文
    mSrcVideoCodecCtx = avcodec_alloc_context3(codec);
    if (!mSrcVideoCodecCtx) {
        handleFFmpegError(-1,"无法分配解码器上下文内存");
        return false;
    }

    // 复制编解码器参数
    ret = avcodec_parameters_to_context(mSrcVideoCodecCtx, mSrcVideoStream->codecpar);
    if (!handleFFmpegError(ret, "复制编解码器参数")) {
        return false;
    }

    // 打开解码器
    ret = avcodec_open2(mSrcVideoCodecCtx, codec, nullptr);
    if (!handleFFmpegError(ret, "打开解码器")) {
        return false;
    }

    mSrcVideoWidth = mSrcVideoCodecCtx->width;
    mSrcVideoHeight = mSrcVideoCodecCtx->height;

    return true;
}

bool CodeThread::initializeDestination()
{
    if (mDstUrl.isEmpty()) {
        handleFFmpegError(-1,"目标URL为空");
        return false;
    }

    // 创建输出上下文
    int ret = avformat_alloc_output_context2(&mDstFmtCtx, NULL, "rtsp", mDstUrl.toLocal8Bit().data());
    if (!handleFFmpegError(ret, "创建输出上下文")) {
        return false;
    }

    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        handleFFmpegError(-1,"找不到H.264编码器");
        return false;
    }

    mDstVideoCodecCtx = avcodec_alloc_context3(codec);
    if (!mDstVideoCodecCtx) {
        handleFFmpegError(-1,"无法分配编码器上下文内存");
        return false;
    }

    // 设置编码器参数
    mDstVideoCodecCtx->codec_id = codec->id;
    mDstVideoCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO; // 设置编码器类型
    mDstVideoCodecCtx->width = mDstVideoWidth;          // 设置视频宽度
    mDstVideoCodecCtx->height = mDstVideoHeight;        // 设置视频高度

    mDstVideoCodecCtx->bit_rate = mBitrate;             // 设置基础比特率
    mDstVideoCodecCtx->rc_buffer_size = mBufferSize;    // 设置缓冲区大小（用于控制编码延迟）
    mDstVideoCodecCtx->rc_max_rate = mMaxBitrate;       // 设置最大比特率约束
    mDstVideoCodecCtx->rc_min_rate = mMinBitrate;       // 设置最小比特率约束

    mDstVideoCodecCtx->time_base = {1, mDstVideoFps};   // 设置时间基
    mDstVideoCodecCtx->framerate = {mDstVideoFps, 1};   // 设置帧率
    mDstVideoCodecCtx->gop_size = 30;                   // I帧间隔（网络状况特别好，也可以适当增加到45（1.5秒）。如果网络条件不佳，可以考虑减小到20-25）
    mDstVideoCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;    // 设置像素格式
    mDstVideoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // 打印编码器参数配置
    LogInfo <<QString("【编码器】打印编码器参数配置:")<<endl
            <<QString("编码器ID: %1").arg(mDstVideoCodecCtx->codec_id)<<endl
            <<QString("编码器类型: %1").arg(mDstVideoCodecCtx->codec_type)<<endl
            <<QString("视频宽度: %1").arg(mDstVideoCodecCtx->width)<<endl
            <<QString("视频高度: %1").arg(mDstVideoCodecCtx->height)<<endl
            <<QString("目标码率: %1").arg(mDstVideoCodecCtx->bit_rate)<<endl
            <<QString("RC缓冲区大小: %1").arg(mDstVideoCodecCtx->rc_buffer_size)<<endl
            <<QString("RC最大码率: %1").arg(mDstVideoCodecCtx->rc_max_rate)<<endl
            <<QString("RC最小码率: %1").arg(mDstVideoCodecCtx->rc_min_rate)<<endl
            <<QString("时间基: %1/%2").arg(mDstVideoCodecCtx->time_base.num).arg(mDstVideoCodecCtx->time_base.den)<<endl
            <<QString("帧率: %1/%2").arg(mDstVideoCodecCtx->framerate.num).arg(mDstVideoCodecCtx->framerate.den)<<endl
            <<QString("I帧间隔: %1").arg(mDstVideoCodecCtx->gop_size)<<endl
            <<QString("像素格式: %1").arg(mDstVideoCodecCtx->pix_fmt)<<endl
            <<QString("标志: %1").arg(mDstVideoCodecCtx->flags);




    // 创建编码器选项字典
    AVDictionary* codec_options = nullptr;

    // 基础预设
    av_dict_set(&codec_options, "preset", "superfast", 0);
    av_dict_set(&codec_options, "tune", "zerolatency", 0);

    // 根据不同的比特率控制模式设置参数
    if (mRateControl == "cbr") {
        // 固定比特率模式 (CBR)
        av_dict_set(&codec_options, "nal-hrd", "cbr", 0);    // CBR模式
        av_dict_set(&codec_options, "x264-params",
                    QString("nal-hrd=cbr:force-cfr=1:vbv-maxrate=%1:vbv-bufsize=%2")
                        .arg(mMaxBitrate/1000)  // 转换为 kbps
                        .arg(mBufferSize/1000)
                        .toStdString().c_str(), 0);
    }
    else if (mRateControl == "vbr") {
        // 可变比特率模式 (VBR)
        av_dict_set(&codec_options, "crf", "23", 0);         // 质量因子，范围0-51，值越小质量越好
        av_dict_set(&codec_options, "maxrate",
                    QString::number(mMaxBitrate).toStdString().c_str(), 0);
        av_dict_set(&codec_options, "bufsize",
                    QString::number(mBufferSize).toStdString().c_str(), 0);
    }
    else if (mRateControl == "abr") {
        // 平均比特率模式 (ABR)
        av_dict_set(&codec_options, "b:v",
                    QString::number(mBitrate).toStdString().c_str(), 0);
        av_dict_set(&codec_options, "maxrate",
                    QString::number(mMaxBitrate).toStdString().c_str(), 0);
        av_dict_set(&codec_options, "minrate",
                    QString::number(mMinBitrate).toStdString().c_str(), 0);
    }

    // 打开编码器
    ret = avcodec_open2(mDstVideoCodecCtx, codec, &codec_options);
    if (!handleFFmpegError(ret, "打开编码器")) {
        av_dict_free(&codec_options);
        return false;
    }
    av_dict_free(&codec_options);


    // 创建新的视频流
    mDstVideoStream = avformat_new_stream(mDstFmtCtx, codec);
    if (!mDstVideoStream) {
        handleFFmpegError(-1,"无法创建新的视频流");
        return false;
    }

    mDstVideoStream->id = 0;
    mDstVideoIndex = mDstVideoStream->index;
    mDstVideoStream->time_base = mDstVideoCodecCtx->time_base;

    // 复制编码器参数到视频流
    ret = avcodec_parameters_from_context(mDstVideoStream->codecpar, mDstVideoCodecCtx);
    if (!handleFFmpegError(ret, "复制编码器参数到视频流")) {
        return false;
    }

    // 初始化音频编码器
    const AVCodec* audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!audioCodec) {
        handleFFmpegError(-1, "找不到AAC编码器");
        return false;
    }

    m_audioCodecCtx = avcodec_alloc_context3(audioCodec);
    if (!m_audioCodecCtx) {
        handleFFmpegError(-1, "无法分配音频编码器上下文");
        return false;
    }

    // 设置音频编码器参数
    m_audioCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
    m_audioCodecCtx->codec_id = AV_CODEC_ID_AAC;
    m_audioCodecCtx->sample_rate = m_audioSampleRate;
    m_audioCodecCtx->channel_layout = av_get_default_channel_layout(m_audioChannels);
    m_audioCodecCtx->channels = m_audioChannels;
    m_audioCodecCtx->sample_fmt = m_audioSampleFmt;
    m_audioCodecCtx->bit_rate = 64000; // 64kbps
    m_audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    m_audioCodecCtx->time_base = {1,m_audioSampleRate};

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "strict", "experimental", 0);

    if (avcodec_open2(m_audioCodecCtx, audioCodec, &opts) < 0) {
        handleFFmpegError(-1, "无法打开音频编码器");
        av_dict_free(&opts);
        return false;
    }
    av_dict_free(&opts);

    // 创建音频流
    m_audioStream = avformat_new_stream(mDstFmtCtx, audioCodec);
    if (!m_audioStream) {
        handleFFmpegError(-1, "无法创建音频流");
        return false;
    }
    m_audioStream->time_base = {1,m_audioSampleRate};

    m_audioIndex = m_audioStream->index;
    // 复制编码器参数到音频流
    avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodecCtx);

    // 设置RTSP选项
    AVDictionary* format_options = nullptr;
    av_dict_set(&format_options, "rtsp_transport", "tcp", 0);
    av_dict_set(&format_options, "stimeout", "3000000", 0);
    av_dict_set(&format_options, "rw_timeout", "3000000", 0);



    // 打开输出URL
    if (!(mDstFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&mDstFmtCtx->pb, mDstUrl.toLocal8Bit().data(), AVIO_FLAG_WRITE);
        if (!handleFFmpegError(ret, "打开输出URL")) {
            return false;
        }
    }

    // 写入文件头
    ret = avformat_write_header(mDstFmtCtx, &format_options);
    if (!handleFFmpegError(ret, "写入文件头")) {
        av_dict_free(&format_options);
        return false;
    }
    av_dict_free(&format_options);
    av_dump_format(mDstFmtCtx, 0, mDstUrl.toLocal8Bit().data(), 1);

    emit audioContextReady(mDstFmtCtx, m_audioCodecCtx, m_audioStream);

    // 初始化同步相关
    m_audioBasePts = 0;
    m_videoBasePts = 0;
    m_syncInitialized = false;

    return true;
}

void CodeThread::encodeAudioFrame(AVFrame* frame) {
    QMutexLocker locker(&mMutex);

    if (!mRunning || !frame) {
        av_frame_free(&frame);
        return;
    }

    // 处理第一个音频帧
    {
        QMutexLocker syncLocker(&m_syncMutex);
        if (m_waitingForFirstAudioFrame) {
            m_firstAudioPts = frame->pts;
            m_waitingForFirstAudioFrame = false;
            LogInfo << QString("【同步】收到第一个音频帧 PTS:%1").arg(m_firstAudioPts);

            // 如果视频帧已经开始，启用同步
            if (m_firstVideoPts != AV_NOPTS_VALUE) {
                m_syncInitialized = true;
                emit syncStatusChanged(true);
                LogInfo << "【同步】音视频同步已启用 - 首个音频PTS:" << m_firstAudioPts
                       << ", 首个视频PTS:" << m_firstVideoPts;
            }
        }
        // 更新当前音频帧PTS
        m_audioBasePts = frame->pts;
    }
    int ret = avcodec_send_frame(m_audioCodecCtx, frame);
    av_frame_free(&frame);

    if (ret < 0) {
        handleFFmpegError(ret, "发送音频帧到编码器失败");
        return;
    }

    while (ret >= 0) {
        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.data = nullptr;
        pkt.size = 0;

        ret = avcodec_receive_packet(m_audioCodecCtx, &pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            handleFFmpegError(ret, "从音频编码器接收数据包失败");
            break;
        }

        pkt.stream_index = m_audioIndex;
        av_packet_rescale_ts(&pkt, m_audioCodecCtx->time_base, m_audioStream->time_base);

        if (av_interleaved_write_frame(mDstFmtCtx, &pkt) < 0) {
            handleFFmpegError(-1, "写入音频数据包失败");
        }
        LogDebug << "写入音频数据包，pts"<<m_audioBasePts;

        av_packet_unref(&pkt);
    }
    QMutexLocker waitLocker(&m_syncWaitMutex);
    m_syncWaitCond.wakeAll();  // 唤醒所有等待视频同步的线程
}

bool CodeThread::inferOutputFormat()
{
    if (mDstUrl.isEmpty()) {
        LogErr << "【编码器】目的流链接为空";
        return false;
    }

    QUrl url(mDstUrl);
    QString scheme = url.scheme().toLower();
    QString path = url.path().toLower();

    // 根据URL scheme设置输出格式
    // 重要修改：对RTSP进行特殊处理
    if (scheme == "rtsp") {
        mOutputFormat = "rtsp";
        return true;
    }
    else if (scheme == "rtmp" || scheme == "rtmps") {
        mOutputFormat = "rtmp";
    }
    else if (scheme == "udp" || scheme == "rtp") {
        mOutputFormat = "mpegts";
    }
    else {
        // 根据文件扩展名设置输出格式
        QFileInfo fileInfo(path);
        QString ext = fileInfo.suffix().toLower();

        if (ext == "mp4") {
            mOutputFormat = "mp4";
        }
        else if (ext == "flv") {
            mOutputFormat = "flv";
        }
        else if (ext == "ts") {
            mOutputFormat = "mpegts";
        }
        else if (ext == "mkv") {
            mOutputFormat = "matroska";
        }
        else {
            // 默认使用 MP4 格式
            mOutputFormat = "mp4";        }
    }
    return true;
}

bool CodeThread::handleFFmpegError(int errorCode, const QString &operation)
{
    if (errorCode >= 0) {
        return true;
    }
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errorCode, errbuf, AV_ERROR_MAX_STRING_SIZE);
    QString errorMsg = QString("【编码器】%1 失败: %2 (错误码: %3)").arg(operation).arg(errbuf).arg(errorCode);
    emit error(errorMsg);
    return false;
}

AVStream *CodeThread::audioStream() const
{
    return m_audioStream;
}

AVCodecContext *CodeThread::audioCodecCtx() const
{
    return m_audioCodecCtx;
}

AVFormatContext *CodeThread::dstFmtCtx() const
{
    return mDstFmtCtx;
}


bool CodeThread::processNextFrame(AVFrame* srcFrame, AVFrame* dstFrame, SwsContext* swsCtx)
{
    AVPacket packet;
    av_init_packet(&packet);

    int ret = av_read_frame(mSrcFmtCtx, &packet);
    if (!handleFFmpegError(ret, "读取视频帧")) {
        return false;
    }

    if (packet.stream_index == mSrcVideoIndex) {

        // 发送数据包到解码器
        ret = avcodec_send_packet(mSrcVideoCodecCtx, &packet);
        if (!handleFFmpegError(ret, "发送数据包到解码器")) {
            av_packet_unref(&packet);
            return false;
        }

        // 从解码器接收解码后的帧
        ret = avcodec_receive_frame(mSrcVideoCodecCtx, srcFrame);
        if (!handleFFmpegError(ret, "从解码器接收帧")) {
            av_packet_unref(&packet);
            return false;
        }

        // 图像格式转换
        ret = sws_scale(swsCtx,
                        srcFrame->data, srcFrame->linesize, 0, mSrcVideoHeight,
                        dstFrame->data, dstFrame->linesize);
        if (ret < 0) {
            handleFFmpegError(-1,"图像格式转换失败");
            av_packet_unref(&packet);
            return false;
        }

        QMutexLocker locker(&m_syncMutex);

        // 如果还在等待第一个音频帧
        if (m_waitingForFirstAudioFrame) {
            // 记录开始等待时间
            if (m_startWaitTime == 0) {
                m_startWaitTime = av_gettime();
                LogInfo << "【同步】开始等待第一个音频帧：" << m_startWaitTime;
            }

            // 检查是否超时
            int64_t currentTime = av_gettime();
            if (currentTime - m_startWaitTime > MAX_WAIT_TIME_US) {
                LogWarn << "【同步】等待音频帧超时，继续处理视频帧 ,"<<currentTime;
                m_waitingForFirstAudioFrame = false;
                m_syncInitialized = false; // 不使用音视频同步
                // 重置视频帧计数器
                m_videoFrameCount = 0;
                m_firstVideoPts = 0;
            } else {
                // 继续等待，丢弃当前视频帧
                LogDebug << "【同步】等待第一个音频帧，丢弃视频帧";
                av_packet_unref(&packet);
//                msleep(10); // 短暂等待
                return true;
            }
        }

        // 重要修改：使用实际编码的帧数作为PTS
        int64_t currentVideoPts = m_videoFrameCount++;

        // 记录第一个视频帧的PTS（应该总是0）
        if (m_firstVideoPts == AV_NOPTS_VALUE) {
            m_firstVideoPts = 0;
            LogInfo << "【同步】记录第一个视频帧 PTS:" << m_firstVideoPts;
        }

        if (m_syncInitialized) {
            // 视频/音频相对时间（以微秒为单位）
            int64_t videoPtsUs = av_rescale_q(currentVideoPts - m_firstVideoPts,
                                              {1, mDstVideoFps}, {1, AV_TIME_BASE});
            int64_t audioPtsUs = av_rescale_q(m_audioBasePts - m_firstAudioPts,
                                              {1, m_audioSampleRate}, {1, AV_TIME_BASE});
            int64_t diffUs = videoPtsUs - audioPtsUs;
            int waitMs = qMin((int)(diffUs / 1000), 20);
            // 超前：视频太快
            if (waitMs > SYNC_THRESHOLD_MS) {
                if (waitMs > SYNC_MAX_WAIT_MS) {
                    LogWarn << QString("【同步】视频帧超前 %1us，超过最大等待时间，丢弃").arg(diffUs);
                    av_packet_unref(&packet);
                    return true; // 丢帧
                } else {
                    LogDebug << QString("【同步】视频帧超前 %1us，等待音频追上").arg(diffUs);
//                    msleep(diffUs / 1000); // sleep（但上限已经限制）
                    QMutexLocker waitLocker(&m_syncWaitMutex);
                    m_syncWaitCond.wait(&m_syncWaitMutex, waitMs);
                }
            }

            // 滞后：视频太慢
            if (waitMs < -SYNC_THRESHOLD_MS) {
                LogWarn << QString("【同步】视频帧滞后 %1us，丢弃").arg(-diffUs);
                av_packet_unref(&packet);
                return true;
            }
        }

        // 只有真正要编码的帧才设置PTS并递增计数器
        dstFrame->pts = currentVideoPts;

        // 发送帧到编码器
        ret = avcodec_send_frame(mDstVideoCodecCtx, dstFrame);
        if (!handleFFmpegError(ret, "发送帧到编码器")) {
            av_packet_unref(&packet);
            return false;
        }

        while (ret >= 0) {
            AVPacket outPacket;
            av_init_packet(&outPacket);
            outPacket.data = nullptr;
            outPacket.size = 0;

            ret = avcodec_receive_packet(mDstVideoCodecCtx, &outPacket);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_packet_unref(&outPacket);
                break;
            }
            if (!handleFFmpegError(ret, "从编码器接收数据包")) {
                av_packet_unref(&outPacket);
                av_packet_unref(&packet);
                return false;
            }

            outPacket.stream_index = mDstVideoStream->index;

            // 转换时间戳
            outPacket.pts = av_rescale_q(outPacket.pts,
                                         mDstVideoCodecCtx->time_base,
                                         mDstVideoStream->time_base);
            outPacket.dts = av_rescale_q(outPacket.dts,
                                         mDstVideoCodecCtx->time_base,
                                         mDstVideoStream->time_base);
            outPacket.duration = av_rescale_q(outPacket.duration,
                                              mDstVideoCodecCtx->time_base,
                                              mDstVideoStream->time_base);

            // 写入数据包
            ret = av_interleaved_write_frame(mDstFmtCtx, &outPacket);
            if (!handleFFmpegError(ret, "写入数据包")) {
                av_packet_unref(&outPacket);
                av_packet_unref(&packet);
                return false;
            }

            av_packet_unref(&outPacket);
        }
    }

    av_packet_unref(&packet);
    return true;
}

void CodeThread::synchronizeFrames() {
    // 实现帧同步逻辑
    QMutexLocker locker(&m_syncMutex);

    if (!m_syncInitialized && m_audioBasePts > 0) {
        m_syncInitialized = true;
        emit syncStatusChanged(true);
        LogInfo << "【同步】音视频同步已初始化";
    }
}

void CodeThread::onAudioTimestampUpdated(int64_t audioPts) {
    QMutexLocker locker(&m_syncMutex);
    m_audioBasePts = audioPts;
}

void CodeThread::cleanup() {
    if (mSrcVideoCodecCtx) {
        avcodec_flush_buffers(mSrcVideoCodecCtx);  // 刷新解码器缓冲区
        avcodec_free_context(&mSrcVideoCodecCtx);
        mSrcVideoCodecCtx = nullptr;
    }
    if (mDstVideoCodecCtx) {
        avcodec_flush_buffers(mDstVideoCodecCtx);  // 刷新编码器缓冲区
        avcodec_free_context(&mDstVideoCodecCtx);
        mDstVideoCodecCtx = nullptr;
    }
    if (mSrcFmtCtx) {
        avformat_close_input(&mSrcFmtCtx);
        mSrcFmtCtx = nullptr;
    }
    if (mDstFmtCtx) {
        if (mDstFmtCtx->pb) {
            avio_closep(&mDstFmtCtx->pb);
        }
        avformat_free_context(mDstFmtCtx);
        mDstFmtCtx = nullptr;
    }
    // 重置同步状态
    QMutexLocker locker(&m_syncMutex);
    m_waitingForFirstAudioFrame = true;
    m_firstAudioPts = AV_NOPTS_VALUE;
    m_firstVideoPts = AV_NOPTS_VALUE;
    m_syncInitialized = false;
    m_startWaitTime = 0;
    m_audioBasePts = 0;
    m_videoBasePts = 0;
    m_videoFrameCount = 0;
}
