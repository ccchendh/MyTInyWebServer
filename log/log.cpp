#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/time.h>

#include "log.h"

Log::Log(){
    m_count = 0;
    m_is_async = false;
}

Log::~Log(){
    if(m_log_buf){
        delete [] m_log_buf;
    }
    if(m_fp){
        fclose(m_fp);
    }
    if(m_log_queue)
        delete m_log_queue;
}

bool Log::init(const char* filename, int log_max_line, int buf_size, int m_max_size_queue ){
    m_log_buf_size = buf_size;
    m_log_max_line = log_max_line;
    if(m_max_size_queue > 0){
        m_log_queue = new common_queue<std::string>(m_max_size_queue);
        if(m_log_queue == NULL)
            return false;
        m_is_async = true;
        pthread_t tid;
        int ret;
        if((ret = pthread_create(&tid, NULL, flush_log_thread, NULL)) != 0){
            return false;
        }
        if((ret = pthread_detach(tid))){
            return false;
        }
    }
    m_log_buf = new char[m_log_buf_size];
    memset(m_log_buf, '\0', m_log_buf_size);

    time_t t = time(NULL);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char*p = strchr(filename, '/');
    char log_full_name[256] = {0};

    if(p == NULL){
        memset(dir_name, '\0', sizeof(dir_name));
        strcpy(log_name, filename);
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year+1900, my_tm.tm_mon+1, my_tm.tm_mday, filename);
    }
    else{
        strcpy(log_name, p+1);
        strncpy(dir_name, filename, p-filename+1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year+1900, my_tm.tm_mon+1, my_tm.tm_mday, filename);
    }
    m_today = my_tm;

    m_fp = fopen(log_full_name, "a");
    if(m_fp == NULL){
        return false;
    }

    return true;
}

void Log::async_write_log(){
    std::string single_string;
    while(m_log_queue->pop(single_string)){
        m_mutex.lock();
        fputs(single_string.c_str(), m_fp);
        m_mutex.unlock();
    }
}

void Log::write_log(int level, const char* format, ...){
    struct timeval now = {0,0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    char s[16] = {0};
    switch(level){

        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[error]:");
            break;
        default:
            strcpy(s, "[other]:");
            break;
    }

    m_mutex.lock();
    m_count++;
    
    if(!(m_today.tm_year == my_tm.tm_year && m_today.tm_yday == my_tm.tm_yday && m_today.tm_mon == my_tm.tm_mon)
        || m_count % m_log_max_line == 0){
            char new_log[256] = {0};
            fflush(m_fp);
            fclose(m_fp);

            char tail[16] = {0};


            if(!(m_today.tm_year == my_tm.tm_year && m_today.tm_yday == my_tm.tm_yday && m_today.tm_mon == my_tm.tm_mon)){
                snprintf(new_log, 255, "%s%d_%02d_%02d_%s", dir_name,  my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
                m_today = my_tm;
                m_count = 0;
            }
            else{
                snprintf(new_log, 255, "%s%d_%02d_%02d_%s.%lld", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name, m_count / m_log_max_line);
            }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    va_list valist;
    va_start(valist, format);

    std::string log_str;

    m_mutex.lock();

    int n = snprintf(m_log_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    int m = vsnprintf(m_log_buf+n, m_log_buf_size-1, format, valist);
    m_log_buf[n+m] = '\n';
    m_log_buf[n+m+1] = '\0';
    log_str = m_log_buf;

    m_mutex.unlock();

    if(m_is_async){
        m_log_queue->push(log_str);
    }
    else{
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();        
    }

    va_end(valist);

}

void Log::flush(){
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}
