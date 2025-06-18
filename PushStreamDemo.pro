QT       += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

INCLUDEPATH += $$PWD/include \
    $$PWD/include/FFmpeg \
    $$PWD/LogDemo \
    $$PWD/Push

win32:CONFIG(release, debug|release): DESTDIR += $$PWD/bin/Release
else:win32:CONFIG(debug, debug|release): DESTDIR += $$PWD/bin/Debug

SOURCES += \
    LogDemo/Logger.cpp \
    Push/audiocapturethread.cpp \
    Push/audiocodethread.cpp \
    Push/rtspsyncpush.cpp \
    Push/videocapturethread.cpp \
    Push/videocodethread.cpp \
    audioprocessor.cpp \
    codethread.cpp \
    main.cpp \
    mainwindow.cpp \
    rtsppusher.cpp

HEADERS += \
    DataStruct.h \
    LogDemo/Logger.h \
    LogDemo/LoggerTemplate.h \
    Push/audiocapturethread.h \
    Push/audiocodethread.h \
    Push/rtspsyncpush.h \
    Push/videocapturethread.h \
    Push/videocodethread.h \
    audioprocessor.h \
    codethread.h \
    mainwindow.h \
    rtsppusher.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# msvc >= 2017  编译器使用utf-8编码
msvc {
    greaterThan(QMAKE_MSC_VER, 1900){
        QMAKE_CFLAGS += /utf-8
        QMAKE_CXXFLAGS += /utf-8
    }
}

DEPENDPATH += $$PWD/lib \
              $$PWD/lib/FFmpeg \

LIBS += -L$$PWD/lib/FFmpeg/ -lavcodec -lavfilter -lavformat -lswscale -lavutil -lswresample -lavdevice
