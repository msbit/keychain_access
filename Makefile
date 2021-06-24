TARGET    = keychain_access
VERSION   = v2
REV       = $(shell git rev-parse --short=4 HEAD || cat git-rev)

DEBUG     = -g
# DEBUG   = -O2
DEFINES   = -DKCA_VERSION=\"$(VERSION)\"

# If we somehow found a revision number
ifneq ($(REV),)
DEFINES  += -DKCA_REV=\"$(REV)\"
endif


CFLAGS    = -pipe -std=c99 -Wall -pedantic $(DEBUG) $(DEFINES) $(shell pkg-config --cflags libcrypto)
SRC_FILES = $(wildcard *.c)
O_FILES   = $(SRC_FILES:%.c=%.o)
LDFLAGS   = $(shell pkg-config --libs-only-L libcrypto)
LDLIBS    = -framework Security -framework CoreFoundation $(shell pkg-config --libs-only-l libcrypto)


.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(O_FILES)

clean:
	rm -f *.o $(TARGET)

run: $(TARGET)
	./$(TARGET)

install:
	@echo No yet implemented, just copy the file yourself.
