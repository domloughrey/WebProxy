CC = gcc
CFLAGS = -O3 -Wall
OBJS = htproxy.o utils.o cache.o

htproxy: $(OBJS)
	$(CC) $(CFLAGS) -o htproxy $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f htproxy *.o
