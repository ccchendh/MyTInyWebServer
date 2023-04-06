#ifndef COMMON_QUEUE_H
#define COMMON_QUEUE_H

#include <sys/time.h>
#include <queue>
#include <exception>
#include "../lock/locker.h"


template<class T>
class common_queue{

public:

    common_queue(int max_size = 1000) : m_max_size(max_size){
        if(m_max_size <= 0)
            throw std::exception();
    }

    ~common_queue(){

    }

    void clear(){
        m_mutex.lock();
        while(!m_queue.empty())
        {
            m_queue.pop();
        }
        m_mutex.unlock();
    }

    bool full(){
        bool isfull = true;

        m_mutex.lock();
        isfull = (m_queue.size() == m_max_size);
        m_mutex.unlock();
        return isfull;
    }

    bool empty(){
        bool isempty = true;

        m_mutex.lock();
        isempty = (0 == m_queue.size());
        m_mutex.unlock();
        return isempty;
    }

    bool front(T& value){
        m_mutex.lock();
        if(0 == m_queue.size()){
            m_mutex.unlock();
            return false;
        }
        value = m_queue.front();

        m_mutex.unlock();
        return true;    
    }

    bool back(T& value){
        m_mutex.lock();
        if(0 == m_queue.size()){
            m_mutex.unlock();
            return false;
        }
        value = m_queue.back();

        m_mutex.unlock();
        return true;    
    }  

    int size(){
        int tmp = 0;

        m_mutex.lock();
        tmp = m_queue.size();

        m_mutex.unlock();
        return tmp;
    }

    int max_size(){
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;

        m_mutex.unlock();
        return tmp;    
    }

    bool push(const T& item){
        m_mutex.lock();

        if(m_queue.size() == m_max_size){
            m_cond.broadcast();

            m_mutex.unlock();
            return false;
        }
        
        m_queue.push(item);
        m_cond.broadcast();

        m_mutex.unlock();
        return true;            
    }

    bool pop(T& item){
        m_mutex.lock();

        while(0 == m_queue.size()){
            if(!(m_cond.wait(m_mutex.get()))){
                m_mutex.unlock();
                return false;
            }
        }
        item = m_queue.front();
        m_queue.pop();
        
        m_mutex.unlock();
        return true;
    }

    bool pop(T& item,  int ms_timeout){
        struct timeval now{0, 0};
        struct timespec time{0, 0};
        gettimeofday(&now, NULL);
        time.tv_sec = now.tv_sec + ms_timeout/1000;
        time.tv_nsec = (ms_timeout%1000) * 1000000 + now.tv_usec * 1000;

        m_mutex.lock();
        while(0 == m_queue.size()){
            if(!(m_cond.timewait(m_mutex.get(), time))){
                m_mutex.unlock();
                return false;
            }
        }
        item = m_queue.front();
        m_queue.pop();
        
        m_mutex.unlock();
        return true;        
    }

    void lock(){
        m_mutex.lock();
    }

    void unlock(){
        m_mutex.unlock();
    }

private:
    locker m_mutex;
    cond m_cond;

private:
    int m_max_size;
    std::queue<T> m_queue;
};

#endif // !COMMON_QUEUE_H

