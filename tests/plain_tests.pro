QT += core gui qml quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = plain_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/arguments.h ../src/document.h ../src/editing.h ../src/highlighter.h ../src/theme.h ../src/translator.h
SOURCES += plain_tests.cpp ../src/arguments.cpp ../src/document.cpp ../src/editing.cpp ../src/highlighter.cpp \
    ../src/theme.cpp ../src/translator.cpp
