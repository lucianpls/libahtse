MODULE = libahtse
TARGET = $(MODULE).so

C_SRC = ahtse_util.cpp
EXP_HEADERS = ahtse.h ahtse_common.h ahtse_httpd.h
HEADERS = $(EXP_HEADERS)

OBJECTS = $(C_SRC:.cpp=.o)

CXXFLAGS = -fPIC -O2 -Wall

DEFINES = -DLINUX -D_REENTRANT -D_GNU_SOURCE $(DEBUG)

LIBS = -L $(PREFIX)/lib -licd

FILES = $(C_SRC) $(HEADERS)

MAKEOPT ?= Makefile.lcl
include $(MAKEOPT)

default : $(TARGET)

$(TARGET)	:	$(OBJECTS) $(HEADERS)
	$(CXX) -shared -o $@ $(OBJECTS) $(LIBS)

%.o	:	%.cpp $(HEADERS)
	$(CXX) -std=c++0x $(CXXFLAGS) $(DEFINES) -I . $(EXTRA_INCLUDES) -I $(EXP_INCLUDEDIR) -pthread -c $< -o $@

install	:	$(TARGET) $(EXP_HEADERS)
	$(SUDO) $(CP) $< $(DEST)
	$(SUDO) $(CP) $(EXP_HEADERS) $(EXP_INCLUDEDIR)

clean	:
	$(RM) -rf $(TARGET) $(OBJECTS) *.o 
