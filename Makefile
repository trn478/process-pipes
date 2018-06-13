all : pfact

pfact : pfact.c
	gcc -Wall -g -o pfact pfact.c -lm

clean :
	rm *.o pfact
