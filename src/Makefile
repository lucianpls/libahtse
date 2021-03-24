MODULE = libahtse
TARGET = $(MODULE).so

C_SRC = ahtse_util.cpp JPEG_codec.cpp PNG_codec.cpp Packer_RLE.cpp JPEG8.cpp JPEG12.cpp LERC_codec.cpp Lerc1Image.cpp
HEADERS = ahtse.h ahtse_common.h ahtse_codecs.h ahtse_httpd.h BitMask2D.h

OBJECTS = $(C_SRC:.cpp=.o) libjpeg12.o

CXXFLAGS = -fPIC -O2 -Wall

DEFINES = -DLINUX -D_REENTRANT -D_GNU_SOURCE $(DEBUG)

LIBS = -ljpeg -lpng -lstdc++

FILES = $(C_SRC) $(HEADERS)

include Makefile.lcl

default : $(TARGET)

$(TARGET)	:	$(OBJECTS)
	$(CC) -shared -o $@ $^

%.o	:	%.cpp
	$(CXX) -std=c++0x $(CXXFLAGS) $(DEFINES) -I . $(INCLUDES) -pthread -c $< -o $@

libjpeg12.o	:	jpeg12-6b/libjpeg12.o
	$(CP) $^ $@

jpeg12-6b/libjpeg12.o	:
	$(MAKE)	-C jpeg12-6b

install	:	$(TARGET)
	$(SUDO) $(CP) $^ $(DEST)
	$(SUDO) $(CP) $(HEADERS) $(HTTPD_INCLUDES_PATH)

clean	:
	$(RM) -rf $(TARGET) $(OBJECTS) *.o 
	$(MAKE) -C jpeg12-6b clean
