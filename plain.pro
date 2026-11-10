QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = plain
TEMPLATE = app

HEADERS += \
    src/arguments.h \
    src/document.h \
    src/editing.h \
    src/highlighter.h \
    src/portalfilepicker.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/arguments.cpp \
    src/document.cpp \
    src/editing.cpp \
    src/highlighter.cpp \
    src/portalfilepicker.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
