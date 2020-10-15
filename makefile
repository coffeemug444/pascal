main: longer-int.c longer-int.h pascal.c
	gcc -O3 longer-int.c pascal.c -o main -lpthread

clean:
	rm main
