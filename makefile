
server: server.o wrap.o block_queue.h http_conn.o lock.h log.o lst_timer.h sql_connection_pool.o threadpool.h 
	g++ log.o server.o lock.h wrap.o block_queue.h sql_connection_pool.o http_conn.o   lst_timer.h  threadpool.h -o server -lpthread -L/www/server/mysql/lib/ -lmysqlclient

server.o: server.cpp wrap.h
	g++ -c server.cpp -o server.o

wrap.o: wrap.cpp wrap.h
	g++ -c wrap.cpp -o wrap.o


http_conn.o: http_conn.cpp http_conn.h
	g++ -c http_conn.cpp -o http_conn.o


sql_connection_pool.o: sql_connection_pool.cpp sql_connection_pool.h 
	g++ -c sql_connection_pool.cpp -o sql_connection_pool.o -L/www/server/mysql/lib/ -lmysqlclient


.PHONY: clean
clean:
	rm -f *.o
