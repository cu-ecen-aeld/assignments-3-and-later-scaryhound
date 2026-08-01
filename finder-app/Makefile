CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror -g
TARGET = writer

all: $(TARGET)

$(TARGET): writer.o
	$(CC) $(CFLAGS) -o $(TARGET) writer.o
	
writer.o: writer.c
	$(CC) $(CFLAGS) -c writer.c
	
clean:
	rm -f $(TARGET) *.o

