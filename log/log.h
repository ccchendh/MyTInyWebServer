#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <string>
#include <time.h>
#include "../queue/common_queue.h"
#include "../lock/locker.h"

class Log{

public:
    static Log* get_instance(){
        static Log instace;
        return &instace;
    }
    static void* flush_log_thread(void* args){
        Log::get_instance()->async_write_log();
        return NULL;
    }

    bool init(const char* filename, int log_max_line = 5000000, int buf_size = 8192, int m_max_size_queue = 0 );
    void write_log(int level, const char* format, ...);
    void flush();

private:
    Log();
    virtual ~Log();
    void async_write_log();

private:
    char dir_name[128];
    char log_name[128];
    int m_log_max_line;
    int m_log_buf_size;
    long long m_count;
    struct tm m_today;
    FILE* m_fp;
    char* m_log_buf;
    common_queue<std::string>* m_log_queue;
    bool m_is_async;
    locker m_mutex;

};

#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif // !LOG_H