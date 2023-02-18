net367: host.o switch.o packet.o man.o main.o net.o
	gcc -o net367 host.o switch.o man.o main.o net.o packet.o

main.o: main.c types.h
	gcc -c main.c

host.o: host.c types.h
	gcc -c host.c 

switch.o: switch.c types.h
	gcc -c switch.c

man.o:  man.c types.h
	gcc -c man.c

net.o:  net.c types.h
	gcc -c net.c

packet.o:  packet.c types.h
	gcc -c packet.c

clean:
	rm *.o

run:
	./net367
