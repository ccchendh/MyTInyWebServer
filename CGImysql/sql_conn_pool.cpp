#include <iostream>
#include <string>
#include <mysql/mysql.h>
#include "sql_conn_pool.h"

sql_conn_pool::sql_conn_pool(){
}

sql_conn_pool::~sql_conn_pool(){
    destoryPool();
}

sql_conn_pool* sql_conn_pool::getInstance(){
    static sql_conn_pool pool;
    return &pool;
}

void sql_conn_pool::init(std::string url, int Port, std::string User, std::string password, std::string DatabaseName, int MaxConn){
    this->url = url;
    this->Port = Port;
    this->User = User;
    this->password = password;
    this->DatabaseName = DatabaseName;
    this->MaxConn = MaxConn;

    for(int i = 0; i<MaxConn; i++){
        MYSQL* con = NULL;
        con = mysql_init(con);
        if(con == NULL)
        {
            std::cout << "Error:" << mysql_error(con);
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), password.c_str(), DatabaseName.c_str(), Port, NULL, 0);
        if(con == NULL)
        {
            std::cout << "Error:" << mysql_error(con);
            exit(1);
        }
        sqlconn_pool.push(con);
    }
}

MYSQL* sql_conn_pool::getConnection(){
    MYSQL* item = NULL;

    sqlconn_pool.pop(item);

    return item;
}

int sql_conn_pool::getFreeConn(){
    return sqlconn_pool.size();
}

bool sql_conn_pool::releaseConnection(MYSQL* conn){
    if(conn == NULL)
        return false;
    return sqlconn_pool.push(conn);
}

void sql_conn_pool::destoryPool(){
    sqlconn_pool.lock();
    MYSQL* item;
    while(!sqlconn_pool.empty()){
        sqlconn_pool.pop(item);
        mysql_close(item);
    }

    sqlconn_pool.unlock();
}

sql_conn_RAII::sql_conn_RAII(MYSQL** SQL_CONN, sql_conn_pool* sql_pool){
    pool = sql_pool;
    sql_conn = pool->getConnection();
    *SQL_CONN = sql_conn;
}

sql_conn_RAII::~sql_conn_RAII(){
    pool->releaseConnection(sql_conn);
}

