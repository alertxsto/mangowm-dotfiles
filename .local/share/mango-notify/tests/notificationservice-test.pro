QT += core gui dbus testlib
CONFIG += c++17 testcase console
INCLUDEPATH += ..
SOURCES += notificationservice-test.cpp ../notificationservice.cpp
HEADERS += ../notificationservice.h
TARGET = notificationservice-test
