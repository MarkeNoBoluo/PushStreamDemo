#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory>
#include "DataStruct.h"
class RTSPPusher;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected slots:
    void handlePusherStateChanged(const QString &objName, PushState state);
private slots:
    void on_btn_Push_clicked();

private:
    Ui::MainWindow *ui;
    std::unique_ptr<RTSPPusher> m_pusher;
    bool m_isPush = false;
};
#endif // MAINWINDOW_H
