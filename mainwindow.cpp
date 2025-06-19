#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "rtsppusher.h"
#include "Logger.h"
#include "rtspsyncpush.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    ,m_isPush(false)
{
    ui->setupUi(this);
    //推流器初始化
//    m_pusher = std::make_unique<RTSPPusher>(this);
//#ifdef Q_OS_WIN
//    m_pusher->setSource("desktop");
//#else
//    m_pusher->setSource(":0.0");
//#endif
//    m_pusher->setVideoSize(1920,1080);
//    m_pusher->setFrameRate(30);
//    m_pusher->setBitRate(6000);//kbps
//    m_pusher->setObjectName("PushStreamer");//投影推流1600
//    connect(m_pusher.get(), &RTSPPusher::stateChanged,
//            this, &MainWindow::handlePusherStateChanged);


    ui->btn_Push->setText("开始");
    ui->lineEdit->setText("rtsp://192.168.42.116:25544/push1");
//    ui->lineEdit->setText("rtsp://127.0.0.1:10054/live");
    m_pushThread = new QThread;
    m_rtspPusher = new RTSPSyncPush;
    m_rtspPusher->moveToThread(m_pushThread);
    connect(m_rtspPusher,&RTSPSyncPush::error,this,[=](QString msg){
        LogErr << msg;
    });
    connect(m_pushThread, &QThread::started, m_rtspPusher, &RTSPSyncPush::start);
    connect(m_pushThread, &QThread::finished, m_rtspPusher, &QThread::deleteLater);
    m_rtspPusher->initialize(
        "desktop",1920,1080,30,4000000,
        44100,2,"rtsp://192.168.42.116:25544/push1"
        );
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::handlePusherStateChanged(const QString &objName, PushState state)
{
    if(objName != "PushStreamer")
        return ;

    LogDebug << "推流状态改变:"<< int(state);
}

void MainWindow::on_btn_Push_clicked()
{
    QString url = ui->lineEdit->text();
    if(url.isEmpty()){
        return ;
    }
    if(m_isPush){
        ui->btn_Push->setText("开始");
        m_pushThread->quit();
//        m_pusher->stop();
    }else{
        ui->btn_Push->setText("停止");
        m_pushThread->start();
//        m_pusher->setDestination(url);
//        m_pusher->start();
    }
    m_isPush = !m_isPush;
}

