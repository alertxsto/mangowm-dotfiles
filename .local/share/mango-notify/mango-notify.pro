QT += core gui qml quick quickcontrols2 dbus network
CONFIG += c++17 link_pkgconfig
PKGCONFIG += libpulse
LIBS += -lLayerShellQtInterface
SOURCES += main.cpp notificationservice.cpp systemcontrols.cpp
HEADERS += notificationservice.h systemcontrols.h
TARGET = mango-notify
