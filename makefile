server: main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/locker.h ./log/log.cpp ./log/log.h ./queue/common_queue.h ./CGImysql/sql_conn_pool.cpp ./CGImysql/sql_conn_pool.h ./timer/lst_timer.h ./timer/lst_timer.cpp 
	g++ -g -o server main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/locker.h ./log/log.cpp ./log/log.h ./CGImysql/sql_conn_pool.cpp ./CGImysql/sql_conn_pool.h ./timer/lst_timer.h ./timer/lst_timer.cpp -lpthread -lmysqlclient


clean:
	rm  -r server
