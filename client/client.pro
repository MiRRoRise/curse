QT += core gui websockets
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

HEADERS += \
    chat_client.hpp

SOURCES += \
    chat_client.cpp \
    main.cpp

# Boost
INCLUDEPATH += /mingw64/include
# Указываем точное имя библиотеки или используем обобщенное с учетом версии
LIBS += -L"D:/msys64/mingw64/lib" -lboost_system-mgw14-mt-d-x64-1_88

# PortAudio
INCLUDEPATH += /mingw64/include
LIBS += -L/mingw64/lib -lportaudio

# Windows-specific libraries for networking
LIBS += -lws2_32

# Compiler flags
QMAKE_CXXFLAGS += -Wall -Wextra -pedantic

# Installation (optional)
target.path = /usr/bin
INSTALLS += target
