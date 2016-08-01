#-------------------------------------------------
#
# Project created by QtCreator 2016-07-29T15:52:35
#
#-------------------------------------------------

QT       += core gui
CCFLAG += -std=c++11

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = test03-v03
TEMPLATE = app


SOURCES += main.cpp\
        widget.cpp webgrep/client_http.cpp

HEADERS  += widget.h webgrep/client_http.hpp

FORMS    += widget.ui

LIBS += -lboost_system -lboost_container -lboost_atomic
