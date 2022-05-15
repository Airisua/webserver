CC = g++
CFLAGES = -pthread -lmysqlclient -g

server: main.cpp http_conn.o sql_connection_pool.o
	$(CC) main.cpp http_conn.o sql_connection_pool.o $(CFLAGES) -o server

http_conn.o:
	$(CC) -c ./http/http_conn.cpp $(CFLAGES)

sql_connection_pool.o:
	$(CC) -c ./pool/sql_connection_pool.cpp $(FGLAGES)

clean: 
	rm -r server *.o
