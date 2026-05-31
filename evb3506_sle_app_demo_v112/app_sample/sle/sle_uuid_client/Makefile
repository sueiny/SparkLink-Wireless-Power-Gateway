CROSS = ../../../gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-
CC	= @echo " GCC	$@"; $(CROSS)gcc
LD	= @echo " LD	$@"; $(CROSS)ld
AR	= @echo " AR	$@"; $(CROSS)ar
RM	= @echo " RM	$@"; rm -f
STRIP	= @echo "STRIP  $@"; $(CROSS)strip

TOPDIR = ./
INCDIR_TOP = $(TOPDIR)/inc/
INCDIR_COMMON = ../../../include/bsle/common/
INCDIR_SLE = ../../../include/bsle/sle/
INCDIR_SEC = ../../../include/libc_sec/include/
INCDIR_DRV = ../../../include/drv/include/
INCDIR_OSAL = ../../../include/osal/include/

SRCDIR = $(TOPDIR)/src/
LIBDIR = $(TOPDIR)/lib/

CFLAGS	+=	-I$(INCDIR_TOP) -I$(INCDIR_COMMON) -I$(INCDIR_SLE) -I$(INCDIR_SEC) -I$(INCDIR_DRV) -I$(INCDIR_OSAL)
CFLAGS	+= -Wall -Os #--std=c++11
LDFLAGS	+= -lm -lpthread -lrt -ldl -Wl,--gc-sections

SRCS := $(wildcard $(SRCDIR)/*.c)
SRCS += $(wildcard $(TOPDIR)/*.c)
OBJS := $(patsubst %.c,%.o,$(filter %.c,$(SRCS)))

TARGET	= $(TOPDIR)/sle_client_sample

all: $(TARGET)

$(TARGET): $(OBJS)
	echo -e "\e[36m<):$@; <):$^\e[0m"
	$(info $($(LIBDIR)/*.a))
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -Xlinker "-(" $(LIBDIR)/*.a -Xlinker "-)"
	$(STRIP) --strip-unneeded -s $@
	
.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS) $(EXEC_OBJS);
	find ./ -type f \( -name '*.elf*' -o -name '*.gdb' \) | xargs rm -f

# $(RM) $@;
# $(STRIP) $@
