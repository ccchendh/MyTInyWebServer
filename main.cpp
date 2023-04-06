#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>

#include "./CGImysql/sql_conn_pool.h"
#include "./http/http_conn.h"
#include "./lock/locker.h"
#include "./log/log.h"
#include "./queue/common_queue.h"
#include "./threadpool/threadpool.h"
#include "./timer/lst_timer.h"
#include "./define/config.h"


extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);
extern void modfd(int epollfd, int fd, int ev);
extern int setnonblocking(int fd);
extern int setreusable(int fd);

static int pipefd[2];
sort_timer_lst timer_lst;
static int epollfd = 0;

void sig_handler(int sig){

    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);//why size_t = 1???
    errno =  save_errno;

}

void addsig(int sig, void(handler(int)), bool restart = true){

    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);

    assert(sigaction(sig, &sa, NULL) != -1);
    
}

void timer_handler(){

    timer_lst.tick();
    alarm(TIMESLOT);
}

void cb_func(http_conn* user_conn){

    assert(user_conn);
    user_conn->close_conn();
    LOG_INFO("close fd %d", user_conn->get_socket());
    Log::get_instance()->flush();
}

void show_error(int connfd, const char* info){
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char* argv[]){

#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 800000, 2000, 8);
#endif // ASYNLOG

#ifdef SYNLOG
    Log::get_instance()->init("ServerLog", 800000, 2000, 0);
#endif // SYNLOG

    if(argc <= 1){

        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;    
    }

    sql_conn_pool* sqlconn_pool = sql_conn_pool::getInstance();
    sqlconn_pool->init("localhost", 3306, "root", "chendonghao1105", "mydb", 8);

    threadpool<http_conn>* thread_pool  =threadpool<http_conn>::getInstance();
    thread_pool->init(sqlconn_pool);

    http_conn* users = new http_conn[MAX_FD];
    assert(users);

    users->initmysql_result(sqlconn_pool);

    int ret = 0;

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_family = AF_INET;
    address.sin_port = htons(atoi(argv[1]));

    setreusable(listenfd);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 5);

    assert(ret >= 0);

    epoll_event events[MAx_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);

    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);

    addsig(SIGPIPE, SIG_IGN);
    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);

    bool stop_server = false;


    bool timeout = false;
    alarm(TIMESLOT);

    while(!stop_server){

        int num = epoll_wait(epollfd, events, MAx_EVENT_NUMBER, -1);
        if(num < 0 && errno != EINTR){
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for(int i = 0; i<num; i++){

            int sockfd = events[i].data.fd;

            if(sockfd == listenfd){

                struct sockaddr_in client_address;
                socklen_t client_addrLen = sizeof(client_address);
#ifdef listenfdLT

                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrLen);
                if(connfd < 0){
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                if(http_conn::m_user_count >= MAX_FD){
                    show_error(connfd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;                
                }

                util_timer* timer = new util_timer;
                users[connfd].init(connfd, client_address, timer);
                timer->user_conn = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 *  TIMESLOT;
                timer_lst.add_timer(timer);
                
#endif

#ifdef listenfdET
                while(1){

                    int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrLen);
                    if(connfd < 0){
                        if(errno == EAGAIN)
                            break;
                        LOG_ERROR("%s:errno is:%d", "accept error", errno);
                        break;
                    }
                    if(http_conn::m_user_count >= MAX_FD){
                        show_error(connfd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;                
                    }

                    users[connfd].init(connfd, client_address);

                    users_timer[connfd].address = client_address;
                    users_timer[connfd].m_http_conn = &users[connfd];
                    users_timer[connfd].sockfd = connfd;
                    util_timer* timer = new util_timer;
                    timer->user_data = &users_timer[connfd];
                    timer->cb_func = cb_func;
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 *  TIMESLOT;
                    users_timer[connfd].timer = timer;
                    timer_lst.add_timer(timer);

                }
                continue;
#endif
            }

            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){

                util_timer* timer = users[sockfd].m_timer;
                timer->cb_func(&users[sockfd]);

                if(timer){

                    timer_lst.del_timer(timer);
                }

            }

            else if(sockfd == pipefd[0] && (events[i].events & EPOLLIN)){

                int sig;
                char signals[1024];

                ret = recv(pipefd[0], signals, sizeof(signals), 0);

                if(ret == -1)
                    continue;
                
                else if(ret == 0)
                    continue;
                
                else{
                    for(int i = 0; i<ret; i++){

                        switch(signals[i]){

                            case SIGALRM:
                                timeout = true;
                                break;
                            case SIGTERM:
                                stop_server = true;
                                break;
                        }

                    }
                }
            }

            else if(events[i].events & EPOLLIN){

                users[sockfd].setRW(RD);
                thread_pool->append(&users[sockfd]);

            }

            else if(events[i].events & EPOLLOUT){

                users[sockfd].setRW(WR);
                thread_pool->append(&users[sockfd]);
                
            }

            if(timeout){

                timer_handler();
                timeout = false;
            }
        }

    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete [] users;

    return 0;
}