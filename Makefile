CC      = cc
CFLAGS  = -O2 -Wall -Wextra

p4: protocol4.c protocol.h
	$(CC) $(CFLAGS) -o p4 protocol4.c

clean:
	rm -f p4

.PHONY: clean
