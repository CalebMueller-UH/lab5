CFLAGS = -g

net367: main.o host.o switch.o man.o net.o packet.o job.o
	gcc $(CFLAGS) -o net367 main.o host.o switch.o man.o net.o packet.o job.o

main.o: main.c job.h net.h
	gcc $(CFLAGS) -c main.c

host.o: host.c job.h net.h
	gcc $(CFLAGS) -c host.c 

switch.o: switch.c job.h net.h
	gcc $(CFLAGS) -c switch.c

man.o:  man.c job.h net.h
	gcc $(CFLAGS) -c man.c

net.o:  net.c job.h net.h
	gcc $(CFLAGS) -c net.c

packet.o:  packet.c job.h net.h
	gcc $(CFLAGS) -c packet.c

job.o: job.c job.h
	gcc $(CFLAGS) -c job.c

clean:
	rm *.o

run:
	./net367
