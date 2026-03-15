CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c11

scripture: scripture.c
	$(CC) $(CFLAGS) -o scripture scripture.c

install: scripture
	cp scripture ~/.local/bin/

clean:
	rm -f scripture
