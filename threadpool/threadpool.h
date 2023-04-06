#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <pthread.h>
#include <stdlib.h>
#include "../queue/common_queue.h"
#include "../CGImysql/sql_conn_pool.h"
#include "../define/config.h"
#include "../log/log.h"
#include "../timer/lst_timer.h"
#include "../http/http_conn.h"

class sort_timer_lst;
class util_timer;

extern sort_timer_lst timer_lst;


template < class T>
class threadpool{

public:
    static threadpool<T>* getInstance(){
        static threadpool<T>  pool;
        return &pool;
    }
    void init(sql_conn_pool *connPool, int max_threads = 8, int max_requests = 1000000);
    ~threadpool();
    bool append(T *request);

private:
    threadpool();
    static void *worker(void *arg);
    void run();

private:
    int m_max_threads;
    int m_max_requests;
    pthread_t* m_threads;
    common_queue<T* >* m_workerqueue;
    bool m_stop;
    sql_conn_pool* m_connPool;
    
};


template<class T>
void threadpool<T>::init(sql_conn_pool *connPool, int max_threads, int max_requests) {
    m_connPool = connPool;
    m_max_threads = max_threads; 
    m_max_requests = max_requests;
    m_threads = NULL;
    m_stop = false;

    if(m_max_requests <= 0 || m_max_threads <= 0){
        std::cout << "threadpool init error : arg invalid!" << std::endl;
        exit(1);
    }

    m_workerqueue = new common_queue<T*>(m_max_requests);
    if(m_workerqueue == NULL){
        std::cout << "threadpool init error : workerqueue init error!" << std::endl;
        exit(1);    
    }

    m_threads = new pthread_t[m_max_threads];
    if(!m_threads)
        throw std::exception();
        
    for(int i = 0; i<m_max_threads; i++)
    {
        if((pthread_create(m_threads + i, NULL, worker, (void *)this)) != 0)
        {
            std::cout << "threadpool init error : failed to create thread!" << std::endl;
            exit(1);
        }
        if(pthread_detach(m_threads[i]))
        {
            std::cout << "threadpool init error : failed to detach thread!" << std::endl;
            exit(1);
        }
    }
}

template<class T>
threadpool<T>::threadpool(){
    m_threads = NULL;
    m_stop = false;
}

template<class T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    delete m_workerqueue;
    m_stop = true;
}

template< class T>
bool threadpool<T>::append(T* request){
    return m_workerqueue->push(request);
}

template<class T>
void* threadpool<T>::worker(void* arg)
{
    threadpool<T>* pool = (threadpool<T>*)arg;
    pool->run();
    return pool;
}

template<class T>
void threadpool<T>::run()
{
    T* request;
    while(!m_stop){
        if(!m_workerqueue->pop(request))
            continue;
        if(!request)
            continue;
            
        sql_conn_RAII mysqlcon(&request->mysql, m_connPool);
        if(request->getRW() == RD){

            util_timer* timer = request->m_timer;
            if(request->read()){

                LOG_INFO("deal with the client(%s)", inet_ntoa(request->get_address()->sin_addr));
                Log::get_instance()->flush();


                if(timer){

                    time_t cur = time(NULL);
                    timer->expire = cur + 3*TIMESLOT;
                    LOG_INFO("%s", "adjust timer once");
                    Log::get_instance()->flush();
                }
            }
            else{

                timer->cb_func(request);
                if(timer){
                    timer_lst.del_timer(timer);
                }
            }

            request->process();    
        }
        else{

            util_timer* timer = request->m_timer;
            if(request->write()){

                LOG_INFO("send data to tthe client(%s)", inet_ntoa(request->get_address()->sin_addr));
                Log::get_instance()->flush();

                if(timer){

                    time_t cur = time(NULL);
                    timer->expire = cur + 3*TIMESLOT;
                    LOG_INFO("%s", "adjust timer once");
                    Log::get_instance()->flush();
                }
            }
            else{

                timer->cb_func(request);
                if(timer){
                    timer_lst.del_timer(timer);
                }
            }
        }
    }

}

#endif // !THREADPOOL_H