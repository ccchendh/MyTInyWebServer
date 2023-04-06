#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <queue>

#include "../http/http_conn.h"
#include "../log/log.h"

class http_conn;

class util_timer{

public:
    util_timer() : deleted(false){}

    void (*cb_func)(http_conn* );

public:
    time_t expire;
    http_conn* user_conn;
    int deleted;
};

struct TimerCmp {
  bool operator()(util_timer* a,
                  util_timer* b) const {
    return a->expire > b->expire;
  }
};

class sort_timer_lst{

public:
    sort_timer_lst(){}
    ~sort_timer_lst();

    void add_timer(util_timer* timer);
    void del_timer(util_timer* timer);

    void tick();


private:
    std::priority_queue<util_timer*, std::vector<util_timer*>, TimerCmp> timer_pq;
};

#endif // !LST_TIMER_H
