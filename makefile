smallsh: smallsh.c
	gcc -o smallsh smallsh.c -std=gnu99

clean:
	rm smallsh
