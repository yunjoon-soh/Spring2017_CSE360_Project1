CFLAGS=-m32 -g -Wall -DLEN1=1024 -DLEN2=512 -DRANDOM=random\(\)
#CFLAGS=-m32 -g -Wall -DLEN1=1024 -DLEN2=512 -DRANDOM=0
vuln: vuln.o my_malloc.o
	gcc $(CFLAGS) -o vuln vuln.o my_malloc.o
	execstack -s vuln

vuln.o: vuln.c my_malloc.h
	gcc $(CFLAGS) -c vuln.c

my_malloc.o: my_malloc.h my_malloc.c
	gcc $(CFLAGS)  -c my_malloc.c

clean:
	rm vuln vuln.o my_malloc.o
