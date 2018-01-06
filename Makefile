all:
	make v330
	make v342

v330:
	gcc -Wall -O2 Editmon330.c -o Editmon330
	gcc -Wall -O2 Makehex330.c -o Makehex330
	gcc -Wall -O2 Makebin330.c -o Makebin330

v342:
	gcc -Wall -O2 Makehex342.c -o Makehex342
