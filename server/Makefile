SRC := aesdsocket.c
TARGET = aesdsocket
OBJS := $(SRC:.c=.o)
CC ?= $(CROSS_COMPILE)gcc
CFLAGS := -g -Wall -Werror

USE_AESD_CHAR_DEVICE ?= 1
ifeq ($(USE_AESD_CHAR_DEVICE), 1)
    CFLAGS += -DUSE_AESD_CHAR_DEVICE=1
else
    CFLAGS += -DUSE_AESD_CHAR_DEVICE=0
endif

LDFLAGS += -pthread

all: $(TARGET)
default: $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	-rm -f *.o $(TARGET) *.elf *.map

