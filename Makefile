tcp-broadcast: tcp-broadcast.c
	indent -kr -i2 -nut $<
	gcc -Werror -Wall -Wextra -ansi -std=c89 -pedantic -Wmissing-prototypes \
	  -Wstrict-prototypes -Wold-style-definition -o $@ $<

all: tcp-broadcast
