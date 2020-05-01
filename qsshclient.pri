QT += core network

CONFIG += c++11

HEADERS += \
    $$files($$PWD/src/*.h) \

SOURCES += \
    $$files($$PWD/src/*.cpp) \

INCLUDEPATH += $$PWD/src/

include($$PWD/3rdparty/3rdparty.pri)
