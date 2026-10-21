QT = core testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
INCLUDEPATH += ..
SOURCES += batterycontroller-test.cpp ../batterycontroller.cpp
HEADERS += ../batterycontroller.h
TARGET = batterycontroller-test
