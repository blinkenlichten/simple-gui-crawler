#-------------------------------------------------
#
# Project created by QtCreator 2016-07-29T15:52:35
#
#-------------------------------------------------

QT       += core gui
CCFLAG += -std=c++11
win32 {
QT += network
}

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = test03-v03
TEMPLATE = app


SOURCES += main.cpp\
        widget.cpp webgrep/client_http.cpp \
    webgrep/crawler.cpp \
    webgrep/crawler_worker.cpp \
    webgrep/linked_task.cpp \
    link_status_widget.cpp \
    webgrep/crawler_private.cpp

HEADERS  += widget.h webgrep/client_http.hpp \
    webgrep/crawler.h \
    webgrep/crawler_worker.h \
    webgrep/linked_task.h \
    link_status_widget.h \
    webgrep/crawler_private.h \
    webgrep/noncopyable.hpp

FORMS    += widget.ui

INCLUDEPATH += ./3rdparty/include
LIBS += -L$$_PRO_FILE_PWD_/3rdparty/lib

unix{
  LIBS += -lboost_system -lboost_container -lboost_context -lboost_thread\
-lssl -lcrypto -lboost_atomic
  LIBS += $$_PRO_FILE_PWD_/3rdparty/lib/libneon.a
}
win32
{
# MinGW32 only, at the moment
libpath = $$_PRO_FILE_PWD_/3rdparty/lib/

#modify for your version:
endian = "-mgw49-mt-1_61.dll.a"

LIBS += $$libpath/libboost_system$$endian \
$$libpath/libboost_container$$endian \
$$libpath/libboost_context$$endian \
$$libpath/libboost_thread$$endian \
$$libpath/libboost_atomic$$endian \
#$$libpath/libssl.a \
#$$libpath/libneon.a \
#$$libpath/libgnutls.dll.a $$libpath/libcrypto.a \
#$$libpath/libhogweed.a $$libpath/libz.a \
#$$libpath/libgmp.dll.a $$libpath/libp11-kit.dll.a $$libpath/libintl.dll.a \
#$$libpath/libproxy.dll.a $$libpath/libpthread.a $$libpath/libws2_32.a $$libpath/libwsock32.a \
#$$libpath/libdl.a $$libpath/librt.a

LIBS += -lpthread

}
