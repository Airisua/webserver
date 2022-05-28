CC = g++
CFLAGES = -pthread -lmysqlclient

server: main_test.cpp http_conn.o sql_connection_pool.o timer_list.o server.o log.o buffer.o
	$(CC) main_test.cpp http_conn.o sql_connection_pool.o timer_list.o server.o log.o buffer.o $(CFLAGES) -o server

server.o:
	$(CC) -c ./epoll/server.cpp $(CFLAGES)

http_conn.o:
	$(CC) -c ./http/http_conn.cpp $(CFLAGES)

timer_list.o:
	$(CC) -c ./timer/timer_list.cpp $(FGLAGES)

sql_connection_pool.o:
	$(CC) -c ./pool/sql_connection_pool.cpp $(FGLAGES)

log.o:
	$(CC) -c ./log/log.cpp $(FGLAGES)

buffer.o:
	$(CC) -c ./buffer/buffer.cpp $(FGLAGES)

clean: 
	rm -r server *.o
