#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <map>
#include <stdio.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <sys/epoll.h>
#include <mysql/mysql.h>

#include "../CGImysql/sql_conn_pool.h"
#include "../timer/lst_timer.h"

class util_timer;

const int FILENAME_LEN = 200;
const int READ_BUFFER_SIZE = 2048;
const int WRITE_BUFFER_SIZE = 1024;


enum METHOD{
    GET = 0,
    POST,
    HEAD,
    PUT,
    DELETE,
    TRACE,
    OPTIONS,
    CONNECT,
    PATH
};

enum CHECK_STATE{
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
};

enum HTTP_CODE{
    NO_REQUEST = 0,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURSR,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION
};

enum LINE_STATUS{
    LINE_OK = 0,
    LINE_BAD,
    LINE_OPEN
};

enum RDORWR{
    RD = 0,
    WR,
};

class http_conn{

public:
    http_conn() {}
    ~http_conn() {}
    void process();
    bool read();
    bool write();
    void setRW(RDORWR rdowr) {m_rdowr = rdowr;}
    RDORWR getRW(){return m_rdowr;}
    sockaddr_in* get_address(){
        return &m_address;
    }
    int get_socket(){
        return m_sockfd;
    }

    void initmysql_result(sql_conn_pool* pool);

    void init(int sockfd, const sockaddr_in &addr, util_timer* timer);
    void close_conn(bool real_close = true);



private:

    void init();

    HTTP_CODE process_read();
    char* get_line(){return m_read_buf+m_start_line;};
    LINE_STATUS parse_line();
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_header(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    void unmap();

    bool process_write(HTTP_CODE);
    bool add_response(const char* format, ...);
    bool add_headers(int content_len);
    bool add_status_line(int status, const char* title);
    bool add_content_length(int content_len);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char* content);



public:
    MYSQL* mysql;
    static int m_epollfd;
    static int m_user_count;
    util_timer* m_timer;
    
private:
    RDORWR m_rdowr;
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    int m_checked_idx;
    int m_start_line;
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    CHECK_STATE m_check_state;
    METHOD m_method;
    char m_real_file[FILENAME_LEN];
    char* m_url;
    char* m_version;
    char* m_host;
    int m_content_length;
    bool m_linger;
    char* m_file_address;
    struct stat m_file_stat;
    struct  iovec m_iv[2];
    int m_iv_count;
    int cgi;
    char* m_string;
    int bytes_to_send;
    int bytes_have_send;

    static std::map<std::string, std::string> users;
    static locker m_lock;
    

};

#endif // !HTTP_CONN_H
