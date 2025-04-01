CC = gcc
CFLAGS = -Wall -O2 `pkg-config --cflags libusb-1.0`
LIBS = `pkg-config --libs libusb-1.0`

TARGET = msxbus_ft232h
SRCS = msxbus_ft232h.c
OBJS = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

