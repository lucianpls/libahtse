MODULE = libahtse
TARGET = .libs/$(MODULE).so

C_SRC = ahtse_util.cpp JPEG_codec.cpp PNG_codec.cpp Packer_RLE.cpp JPEG8.cpp JPEG12.cpp LERC_codec.cpp Lerc1Image.cpp
HEADERS = ahtse.h ahtse_common.h ahtse_codecs.h ahtse_httpd.h BitMask2D.h

OBJECTS = $(C_SRC:.cpp=.lo) libjpeg12.a

CXXFLAGS = -prefer-pic -O2 -Wall

DEFINES = -DLINUX -D_REENTRANT -D_GNU_SOURCE $(DEBUG)

LIBS = -ljpeg -lpng -lstdc++

FILES = $(C_SRC) $(HEADERS)

include Makefile.lcl

default : $(TARGET)

$(TARGET)	:	$(OBJECTS)
	$(LIBTOOL) --mode=link g++ -o $(MODULE).la -rpath $(DEST) -module -avoid-version $^ $(LIBS)

%.lo	:	%.cpp
	$(LIBTOOL) --mode=compile g++ -std=c++0x $(CXXFLAGS) $(DEFINES) $(INCLUDES) -pthread -c -o $@ $< && touch $(@:.lo=.slo)

libjpeg12.a	:	jpeg12-6b/libjpeg12.a
	$(CP) $^ $@

jpeg12-6b/libjpeg12.a	:
	$(MAKE)	-C jpeg12-6b

install	:	$(TARGET)
	$(SUDO) $(CP) $^ $(DEST)
	$(SUDO) $(CP) ahtse.h $(HTTPD_INCLUDES_PATH)

clean	:
	rm -rf $(TARGET) $(OBJECTS) .libs *.o *.lo *.slo *.la jpeg12-6b/*.o jpeg12-6b/libjpeg12.a

