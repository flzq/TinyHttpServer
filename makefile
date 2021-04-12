
server: server.o wrap.o
	g++ server.o wrap.o -o server

server.o: server.cpp wrap.h
	g++ -c server.cpp -o server.o

wrap.o: wrap.cpp wrap.h
	g++ -c wrap.cpp -o wrap.o

.PHONY: clean
clean:
	rm -f *.o
