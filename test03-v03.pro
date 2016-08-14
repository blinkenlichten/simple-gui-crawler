#-------------------------------------------------
#
# Project created by QtCreator 2016-07-29T15:52:35
#
#-------------------------------------------------

QT       += core gui
CCFLAG += -std=c++11


greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

# uncomment to build against QNetwork instead of libneon
# VAR_NO_NEON = 1
defined(VAR_NO_NEON,var) {
    DEFINES += NO_LIBNEON
    QT += network
}

## external dependencies for MinGW
# OpenSSL: libssl.a libcrypto.a (build using mingw/MSYS shell)
# libnettle: libnettle.a libhogweed.a
# NEON: libneon.a

TARGET = test03-v03
TEMPLATE = app


SOURCES += main.cpp widget.cpp link_status_widget.cpp \
         webgrep/client_http.cpp \
    webgrep/crawler.cpp \
    webgrep/crawler_worker.cpp \
    webgrep/linked_task.cpp \
    webgrep/crawler_private.cpp \

HEADERS  += widget.h  link_status_widget.h \
webgrep/client_http.hpp \
    webgrep/crawler.h \
    webgrep/crawler_worker.h \
    webgrep/linked_task.h \
    webgrep/crawler_private.h \
    webgrep/noncopyable.hpp \


defined(VAR_NO_NEON,var) {
message("using QtNetwork")
    SOURCES += webgrep/ch_ctx_win32.cpp
    HEADERS += webgrep/ch_ctx_win32.h
} else {
message("using libneon")
    SOURCES += webgrep/ch_ctx_nix.cpp
    HEADERS += webgrep/ch_ctx_nix.h
}

FORMS    += widget.ui

INCLUDEPATH += ./3rdparty/include
LIBS += -L$$_PRO_FILE_PWD_/3rdparty/lib

unix{
  LIBS += -lboost_system -lboost_container -lboost_context -lboost_thread\
-lssl -lcrypto -lboost_atomic
  !defined(VAR_NO_NEON,var) {
#     LIBS += $$_PRO_FILE_PWD_/3rdparty/lib/libneon.a
     LIBS += -lneon
  }
}
win32{
# MinGW32 only, at the moment
libpath = $$_PRO_FILE_PWD_/3rdparty/lib/

#modify for your version:
endian = "-mgw49-mt-1_61.dll.a"

LIBS += $$libpath/libboost_system$$endian \
$$libpath/libboost_container$$endian \
$$libpath/libboost_context$$endian \
$$libpath/libboost_thread$$endian \
$$libpath/libboost_atomic$$endian \
$$libpath/libintl.a $$libpath/libintl.dll.a \
$$libpath/libproxy.dll.a \
$$libpath/libwsock32.a \
$$libpath/libssp_nonshared.a \
$$libpath/libgdi32.a $$libpath/libws2_32.a  $$libpath/libkernel32.a \
$$libpath/libwsock32.a $$libpath/libcrypto.a $$libpath/libssl.dll.a \
-L$$_PRO_FILE_PWD_/webgrep  \
-lmsvcrt -lkernel32 -lgcc $$libpath/libneon.a $$libpath/libneon.dll.a \
$$libpath/libcrypto.dll.a \
$$libpath/libntdll.a \
$$libpath/libntoskrnl.a \
$$libpath/libiconv.a $$libpath/libiconv.dll.a \
$$libpath/libgnutls.a $$libpath/libgnutls.dll.a \
$$libpath/libgnutls-openssl.a $$libpath/libgnutls-openssl.dll.a \
$$libpath/libgmp.a $$libpath/libgmp.dll.a $$libpath/libp11-kit.dll.a $$libpath/libtasn1.dll.a \
#$$libpath/libcrtdll.a \
#$$libpath/libmingwex.a \



#$$libpath/libssl.a \
#$$libpath/libgnutls.dll.a $$libpath/libcrypto.a \
#$$libpath/libhogweed.a $$libpath/libz.a \
#$$libpath/libgmp.dll.a $$libpath/libp11-kit.dll.a $$libpath/libintl.dll.a \
#$$libpath/libproxy.dll.a $$libpath/libpthread.a $$libpath/libws2_32.a $$libpath/libwsock32.a \
#$$libpath/libdl.a $$libpath/librt.a

LIBS += -lpthread

}
