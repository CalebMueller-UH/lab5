net367: host.o switch.o packet.o man.o main.o net.o job.o
	gcc -o net367 host.o switch.o man.o main.o net.o packet.o job.o

main.o: main.c 
	gcc -c main.c

host.o: host.c job.h
	gcc -c host.c 

switch.o: switch.c job.h
	gcc -c switch.c

man.o:  man.c 
	gcc -c man.c

net.o:  net.c 
	gcc -c net.c

packet.o:  packet.c 
	gcc -c packet.c

job.o: job.c job.h
	gcc -c job.c

clean:
	rm *.o

run:
	./net367
