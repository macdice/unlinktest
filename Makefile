CFLAGS=-Wall -g
LDFLAGS=-pthread

unlinktest: unlinktest.c
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

clean:
	rm -f unlinktest
