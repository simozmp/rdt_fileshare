# -*- Makefile -*-

all: main

main:	src/main/client.c \
		src/main/server.c \
		src/main/file_sharing.c \
		libgbn.a \
		bin \
		obj \
		
	gcc src/main/file_sharing.c \
	-c -I "${PWD}/src" -Wall -ggdb -Werror -o obj/file_sharing.o
	gcc -c src/main/client.c \
	-Wall -ggdb -pthread -lm -I "${PWD}/src"
	gcc client.o obj/file_sharing.o lib/libgbn.a -pthread -lm -o bin/client_main
	gcc -c src/main/server.c \
	-Wall -ggdb -pthread -lm -I "${PWD}/src"
	gcc server.o obj/file_sharing.o lib/libgbn.a -pthread -lm -o bin/server_main
	rm client.o server.o

test_filesend:	src/test_filesend/client.c \
				src/test_filesend/server.c \
				libgbn.a \
				bin \

	gcc -c src/test_filesend/client.c \
		-Wall -ggdb -pthread -lm -I "${PWD}/src"
	gcc client.o lib/libgbn.a -pthread -lm -o bin/testc
	gcc -c src/test_filesend/server.c \
		-Wall -ggdb -pthread -lm -I "${PWD}/src"
	gcc server.o lib/libgbn.a -pthread -lm -o bin/tests
	rm client.o server.o

test_messages:	src/test_messages/gbn_test_c.c \
				src/test_messages/gbn_test_s.c \
				libgbn.a \

	mkdir -p bin
	gcc -c src/test_messages/gbn_test_c.c \
		-Wall -ggdb -pthread -lm -I "${PWD}/src"
	gcc gbn_test_c.o lib/libgbn.a -pthread -lm -o bin/testc
	gcc -c src/test_messages/gbn_test_s.c \
		-Wall -ggdb -pthread -lm -I "${PWD}/src"
	gcc gbn_test_s.o lib/libgbn.a -pthread -lm -o bin/tests
	rm gbn_test_c.o gbn_test_s.o

merge_logs: src/merge_last_logs.c libgbn.a
	gcc src/merge_last_logs.c \
		-o bin/merge_logs
	
test_snd_buffer: src/test_snd_buffer.c libgbn.a snd_buffer.o
	gcc -c src/test_snd_buffer.c \
		-I src/
	gcc test_snd_buffer.o libgbn.a -pthread -lm -o test_buf

libgbn.a: lib gbn_core.o gbn_utils.o packet.o snd_buffer.o rcv_buffer.o
	gcc src/gbn/libgbn.c \
		-c -I "${PWD}/src" -Wall -ggdb -Werror -fpic -o obj/libgbn.o
	ar rs lib/libgbn.a obj/libgbn.o obj/gbn_core.o obj/gbn_utils.o \
		obj/packet.o obj/snd_buffer.o obj/rcv_buffer.o

gbn_core.o: obj
	gcc src/gbn/gbn_core.c \
		-c -I "${PWD}/src" -Wall -ggdb -Werror -o obj/gbn_core.o

gbn_utils.o: obj src/gbn/gbn_utils.c
	gcc src/gbn/gbn_utils.c \
		-c -I "${PWD}/src" -Wall -ggdb -Werror -o obj/gbn_utils.o

packet.o: obj src/gbn/packet.c
	gcc src/gbn/packet.c \
		-c -I "${PWD}/src" -Wall -ggdb -Werror -o obj/packet.o

snd_buffer.o: obj src/gbn/snd_buffer.c
	gcc src/gbn/snd_buffer.c \
		-c -I "${PWD}/src" -Wall -ggdb -Werror -o obj/snd_buffer.o

rcv_buffer.o: obj src/gbn/rcv_buffer.c
	gcc src/gbn/rcv_buffer.c \
		-c -I "${PWD}/src" -Wall -ggdb -Werror -o obj/rcv_buffer.o

obj:
	mkdir obj
	
lib:
	mkdir lib
	
bin:
	mkdir bin

clean: obj lib bin
	rm -r bin obj lib