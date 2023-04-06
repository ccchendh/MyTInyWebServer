#include <map>
#include <utility>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdarg.h>

#include "http_conn.h"
#include "../log/log.h"
#include "../define/config.h"


const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permision to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusuau problem serving the requested file.\n";


const char* doc_root = "/home/chendh4513/Project/TinyWebServer/MyTinyWebServer/root";



int setnonblocking(int fd){
    int oldflag = fcntl(fd, F_GETFL);
    int newflag = oldflag | O_NONBLOCK;
    fcntl(fd, F_SETFL, newflag);
    return oldflag;
}

int setreusable(int fd){
    int flag = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));    
}

void addfd(int epollfd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET
    event.events = EPOLLET | EPOLLIN | EPOLLRDHUP;
#endif 

#ifdef connfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif 

#ifdef listenfdET
    event.events = EPOLLET | EPOLLIN | EPOLLRDHUP;
#endif 

#ifdef listenfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif 

    if(one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, (epoll_event* )NULL);
    close(fd);
}

void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef connfdLT
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);

}

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;
std::map<std::string, std::string> http_conn::users;
locker http_conn::m_lock;

void http_conn::initmysql_result(sql_conn_pool* pool){
    MYSQL* mysql = NULL;
    sql_conn_RAII mysqlconn(&mysql, pool);

    if(mysql_query(mysql, "select username, passwd from user")){
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
        Log::get_instance()->flush();
    }

    MYSQL_RES* result = mysql_store_result(mysql);

    int num = mysql_num_fields(result);

    MYSQL_FIELD* fields = mysql_fetch_field(result);

    while(auto row = mysql_fetch_row(result)){
        std::string tmp1(row[0]);
        std::string tmp2(row[1]);
        users[tmp1] = tmp2;
    }
}

void http_conn::close_conn(bool real_close){
    if(real_close && m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init(int sockfd, const sockaddr_in &addr, util_timer* timer){
    m_sockfd = sockfd;
    m_address = addr;
    m_timer = timer;
    setreusable(sockfd);
    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init();
}

void http_conn::init(){
    m_rdowr = WR;
    mysql = NULL;
    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_write_idx = 0;
    cgi = 0;
    bytes_have_send = 0;
    bytes_to_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_method = GET;
    m_url = NULL;
    m_version = NULL;
    m_host = NULL;
    m_content_length = 0;
    m_linger = false;
    m_file_address = NULL;
    m_iv_count = 0;
    m_string = NULL;

    bzero(&m_file_stat, sizeof(struct stat));
    bzero(&m_iv, 2*sizeof(struct iovec));
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

bool http_conn::read(){

    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

#ifdef connfdET

    while(true){
        int ret = recv(m_sockfd, m_read_buf+m_read_idx, READ_BUFFER_SIZE-m_read_idx,0 );
        if(ret <= 0){
            if(errno==EAGAIN || errno==EWOULDBLOCK)
                break;
            return false;
        }
        m_read_idx += ret;
    }
    return true;
#endif

#ifdef connfdLT

    int ret = recv(m_sockfd, m_read_buf+m_read_idx, READ_BUFFER_SIZE-m_read_idx,0 );
    if(ret <= 0)
        return false;
    m_read_idx += ret;
    return true;

#endif

}

bool http_conn::write(){
    int ret = 0;
    if(bytes_to_send == 0){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }
    while(1){
        ret = writev(m_sockfd, m_iv, m_iv_count);
        if(ret < 0 ){
            if(errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= ret; 
        bytes_have_send += ret;
        if(ret >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else{
            m_iv[0].iov_len = m_iv[0].iov_len - ret;
            m_iv[0].iov_base = (uint8_t *)m_iv[0].iov_base + ret;
        }

        if(bytes_to_send <= 0){
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if(m_linger){
                init();
                return true;
            }
            else
                return false;
        }
    }
}   

void http_conn::process(){
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_conn();
    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    char* text = NULL;
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || (line_status = parse_line()) == LINE_OK){
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        Log::get_instance()->flush();
        HTTP_CODE ret;
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            case CHECK_STATE_HEADER:
                ret = parse_header(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                if(ret == GET_REQUEST)
                    return do_request();
                break;
            case CHECK_STATE_CONTENT:
                ret = parse_content(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                if(ret == GET_REQUEST)
                    return do_request();
                break;     
            default:
                return INTERNAL_ERROR;   
        }
    }

    return NO_REQUEST;
}

LINE_STATUS http_conn::parse_line(){
    char tmp;
    for(; m_check_state<m_read_idx; m_checked_idx++){
        tmp = m_read_buf[m_checked_idx];
        if(tmp == '\r'){
            if(m_checked_idx+1 == m_read_idx)
                return LINE_OPEN;
            else if(m_read_buf[m_checked_idx+1] == '\n'){
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            else
                return LINE_BAD;
        }
        if(tmp == '\n'){
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx-1] == '\r'){
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            else 
                return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

HTTP_CODE http_conn::parse_request_line(char* text){
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char* method = text;
    if(strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if(strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else    
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if(!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0 && strcasecmp(m_version, "HTTP/1.0") != 0)
        return BAD_REQUEST;

    if(strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if(strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0]!='/')
        return BAD_REQUEST;
    if(strlen(m_url) == 1){
        char* m_url1 = "/judge.html";
        m_url = m_url1;
    }
//        strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

HTTP_CODE http_conn::parse_header(char* text){
    if(text[0] == '\0'){
        if(m_content_length == 0)
            return GET_REQUEST;
        else{
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
    }
    else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0)
            m_linger = true;
    }
    else if(strncasecmp(text, "Content-length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "User-Agent:", 11) == 0){

    }
    else{
        LOG_INFO("unknow header: %s", text);
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}

HTTP_CODE http_conn::parse_content(char* text){
    if(m_read_idx >= (m_checked_idx+m_content_length)){
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HTTP_CODE http_conn::do_request(){
    char* p = strchr(m_url, '/');
    strcpy(m_real_file, doc_root);
    if(cgi == 1 && (*(p+1) == '2' || *(p+1) == '3')){
        char flag = *(p+1);
        char name[100];
        char passwd[100];
        int i,j;
        for(i = 5, j = 0; m_string[i]!='&'; i++, j++){
            name[j] = m_string[i];
        }
        name[j] = '\0';
        for(i = i+8, j = 0; m_string[i]!='\0'; i++, j++){
            passwd[j] = m_string[i];
        }
        passwd[i] = '\0';
        //log
        if(flag == '2'){
            if(users.find(name) != users.end() && users[name] == passwd)
                strcat(m_real_file, "/welcome.html");
            else
                strcat(m_real_file, "/logError.html");
        }

        //register
        else if(flag == '3'){
            char* sql_insert = new char[200];
            strcpy(sql_insert, "insert into user(username, passwd) values(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, passwd);
            strcat(sql_insert, "')");
            
            if(users.find(name) == users.end()){
                m_lock.lock();
                int ret = mysql_query(mysql, sql_insert);
                if(ret){
                    m_lock.unlock();
                    strcat(m_real_file, "/registerError.html");
                }
                else{
                    users.insert(std::pair<std::string, std::string>(name, passwd));
                    m_lock.unlock();
                    strcat(m_real_file, "/log.html");
                }
            }
            else{
                strcat(m_real_file, "/registerError.html");
            }
        }
    }
    else if(*(p+1) == '0'){
        strcat(m_real_file, "/register.html");
    }
    else if(*(p+1) == '1'){
        strcat(m_real_file, "/log.html");
    }
    else if(*(p+1) == '5'){
        strcat(m_real_file, "/picture.html");
    }
    else if(*(p+1) == '6'){
        strcat(m_real_file, "/video.html");
    }
    else if(*(p+1) == '7'){
        strcat(m_real_file, "/fans.html");
    }
    else if(cgi != 1 && (*(p+1) == '2' || *(p+1) == '3'))
        return BAD_REQUEST;
    else if(cgi == 1 && *(p+1) != '2' && *(p+1) != '3')
        return BAD_REQUEST;
    else
        strcat(m_real_file, m_url);
    if(stat(m_real_file, &m_file_stat) < 0){
        return NO_RESOURSR;
    }
    if(!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;

}   

void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = NULL;
    }
}

bool http_conn::process_write(HTTP_CODE httpcode){

    switch (httpcode){

    case INTERNAL_ERROR:
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if(!add_content(error_500_form))
            return false;
        break;

    case BAD_REQUEST:
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if(!add_content(error_404_form))
            return false;
        break;


    case NO_RESOURSR:
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if(!add_content(error_404_form))
            return false;
        break;

    case FORBIDDEN_REQUEST:
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if(!add_content(error_403_form))
            return false;
        break;

    case FILE_REQUEST:
        add_status_line(200, ok_200_title);
        if(m_file_stat.st_size != 0){
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx+m_file_stat.st_size;
            return true;
        }
        else{
            const char* ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if(!add_content(ok_string))
                return false;
        }
        break;
    
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;

    return true;
}

//log
bool http_conn::add_response(const char* format, ...){
    if(m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list ap;
    va_start(ap, format);
    int len = vsnprintf(m_write_buf+m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx, format, ap);
    if(len >= WRITE_BUFFER_SIZE-1-m_write_idx){
        va_end(ap);
        return false;
    }
    m_write_idx += len;
    va_end(ap);
    LOG_INFO("request:%s", m_write_buf);
    return true;

}

bool http_conn::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n", m_version, status, title);
}

bool http_conn::add_headers(int content_len){

    return add_content_type() && add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_type(){
    return add_response("Content-Type: %s\r\n","text/html");
}

bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length: %d\r\n",content_len);
}

bool http_conn::add_linger(){
        return add_response("Connection: %s\r\n",(m_linger == true)?"keep-alive":"close");
}

bool http_conn::add_blank_line(){
    return add_response("\r\n");
}

bool http_conn::add_content(const char* content){
    return add_response("%s\r\n", content);
}



