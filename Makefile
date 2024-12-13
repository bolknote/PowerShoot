TARGET = power_shoot
SRC = power_shoot.c

ifeq ($(shell uname), Darwin)
	CFLAGS = -framework IOKit -framework CoreFoundation -Wall
else
	$(error This Makefile is for macOS only)
endif

$(TARGET): $(SRC)
	@gcc $(SRC) $(CFLAGS) -o $(TARGET)

clean:
	@rm -f $(TARGET)
