CC = gcc
CFLAGS = -Wall -Wextra -std=c11

TARGET = myshell

all: $(TARGET)

$(TARGET): myshell.c
	$(CC) $(CFLAGS) myshell.c -o $(TARGET)

clean:
	rm -f $(TARGET) *.o