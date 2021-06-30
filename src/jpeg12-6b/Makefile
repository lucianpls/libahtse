TARGET = libjpeg12.o

C_SRC = jcapimin.c jcapistd.c jccoefct.c jccolor.c jcdctmgr.c jchuff.c \
        jcinit.c jcmainct.c jcmarker.c jcmaster.c jcomapi.c jcparam.c \
        jcphuff.c jcprepct.c jcsample.c jctrans.c jdapimin.c jdapistd.c \
        jdatadst.c jdatasrc.c jdcoefct.c jdcolor.c jddctmgr.c jdhuff.c \
        jdinput.c jdmainct.c jdmarker.c jdmaster.c jdmerge.c jdphuff.c \
        jdpostct.c jdsample.c jdtrans.c jerror.c jfdctflt.c jfdctfst.c \
        jfdctint.c jidctflt.c jidctfst.c jidctint.c jidctred.c jquant1.c \
        jquant2.c jutils.c jmemmgr.c jmemnobs.c

HEADERS = cderror.h jchuff.h jconfig.h jdct.h jdhuff.h jerror.h jinclude.h \
        jmemsys.h jmorecfg.h jpegint.h jpeglib.h jversion.h

OBJECTS = $(C_SRC:.c=.o)

CXXFLAGS = -fPIC -O2 -Wall

DEFINES = -DLINUX -D_REENTRANT -D_GNU_SOURCE $(DEBUG)

FILES = $(C_SRC) $(HEADERS)

include $(MAKEOPT)

default : $(TARGET)

$(TARGET)	:	$(OBJECTS)
	$(LD) -r $^ -o $@

%.o	:	%.c
	$(CXX) $(CXXFLAGS) $(DEFINES) -I . -c $< -o $@

clean	:
	$(RM) -rf $(OBJECTS) $(TARGET)