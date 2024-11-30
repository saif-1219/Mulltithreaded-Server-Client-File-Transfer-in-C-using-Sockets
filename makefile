compile:
	gcc -c -Wall server.c -o server.o
	gcc -c -Wall client.c -o client.o

	 

build:
	gcc -o server server.o -lpthread -lssl -lcrypto
	gcc -o client client.o -lpthread -lssl -lcrypto
	

clean:
	rm -f server client server.o client.o