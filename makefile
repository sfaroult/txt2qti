all: txt2qti

txt2qti: txt2qti.c strbuf.o md5.o miniz.o
	gcc -o txt2qti txt2qti.c strbuf.o md5.o miniz.o

clean:
	/bin/rm *.o
	/bin/rm txt2qti
