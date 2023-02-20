CFLAGS = -g

# Uncomment the following line to turn on debug messages
DEBUG = -DDEBUG

net367: main.o host.o switch.o man.o net.o packet.o job.o
	gcc $(CFLAGS) $(DEBUG) -o net367 main.o host.o switch.o man.o net.o packet.o job.o

main.o: main.c job.h net.h
	gcc $(CFLAGS) $(DEBUG) -c main.c

host.o: host.c job.h net.h
	gcc $(CFLAGS) $(DEBUG) -c host.c 

switch.o: switch.c job.h net.h
	gcc $(CFLAGS) $(DEBUG) -c switch.c

man.o:  man.c job.h net.h
	gcc $(CFLAGS) $(DEBUG) -c man.c

net.o:  net.c job.h net.h
	gcc $(CFLAGS) $(DEBUG) -c net.c

packet.o:  packet.c job.h net.h
	gcc $(CFLAGS) $(DEBUG) -c packet.c

job.o: job.c job.h
	gcc $(CFLAGS) $(DEBUG) -c job.c

clean:
	rm *.o

run:
	./net367
