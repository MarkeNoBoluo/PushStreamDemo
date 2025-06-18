#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include "Logger.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QString logPath = QCoreApplication::applicationDirPath() + "/Log";
    Logger::initLog(logPath, 1024*15, false);
    MainWindow w;
    w.show();
    return a.exec();
}
