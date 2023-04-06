#ifndef SQL_CONN_POOL_H
#define SQL_CONN_POOL_H

#include <mysql/mysql.h>
#include <string>
#include "../queue/common_queue.h"

class sql_conn_RAII;
class sql_conn_pool{

    friend class sql_conn_RAII;

public:
    static sql_conn_pool* getInstance();
    void init(std::string url, int Port, std::string User, std::string password, std::string DatabaseName, int MaxConn);
    int getFreeConn();
    void destoryPool();
    ~sql_conn_pool();

private:
    MYSQL* getConnection();
    bool releaseConnection(MYSQL* conn);
    sql_conn_pool();

private:
    unsigned int MaxConn;

private:
    common_queue<MYSQL*> sqlconn_pool;

private:
    std::string url;
    int Port;
    std::string User;
    std::string password;
    std::string DatabaseName;

};

class sql_conn_RAII{

public:
    sql_conn_RAII(MYSQL** SQL_CONN, sql_conn_pool* sql_pool);
    ~sql_conn_RAII();

private:
    MYSQL* sql_conn;
    sql_conn_pool* pool;

};

#endif // !SQL_CONN_POOL_H


