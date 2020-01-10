all:
	gcc -Os wmmd.c -o wmmd
	strip ./wmmd
