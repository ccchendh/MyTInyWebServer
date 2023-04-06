#include "./lst_timer.h"

sort_timer_lst::~sort_timer_lst(){
    util_timer* tmp;

    while(!timer_pq.empty()){
        tmp = timer_pq.top();
        timer_pq.pop();
        delete tmp;
    }
}

void sort_timer_lst::add_timer(util_timer* timer){

    if(!timer)
        return ;

    timer_pq.push(timer);
}


void sort_timer_lst::del_timer(util_timer* timer){

    if(!timer)
        return;

    timer->deleted = true;
}

//log
void sort_timer_lst::tick(){
    if(timer_pq.empty())   
        return;
    LOG_INFO("%s", "timer tick");
    Log::get_instance()->flush();
    time_t cur = time(NULL);

    util_timer* tmp;

    while(!timer_pq.empty()){
        tmp = timer_pq.top();
        if(tmp->expire > cur && tmp->deleted == false)
            break;
        timer_pq.pop();
        tmp->cb_func(tmp->user_conn);
        delete tmp;
    }
}