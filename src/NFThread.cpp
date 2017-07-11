#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include "NFThread.h"
#include "NFMain.h"
#include "Log.h"

CNFThread::CNFThread():m_FDCount(0),m_pApp(0)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE);
    pthread_mutex_init(&m_MsgsMutex, &attr);
    pthread_mutexattr_destroy(&attr);

    m_ListenD.fd = -1;
    m_UseBalance = 0;

    m_TcpRecvBufSize = 32*1024;
    m_TcpSndBufSize = 8*1024;
    m_TcpKeepAlive = 0;
}

CNFThread::~CNFThread()
{
    pthread_mutex_destroy(&m_MsgsMutex);
}

int CNFThread::InitizlizeThread(int fd_count)
{
    m_Flag = 0;
    m_EP = epoll_create(fd_count);
    if (m_EP == -1) {
        return CODE_ERROR_CREATE_EPOLL;
    }
    NEWARRAY(event_list, epoll_event, MAX_CONNECTIONS_PER_THREAD)

    std::string strfifo("../var/fifo_");
    strfifo += ToString(m_uID);
    if(access(strfifo.c_str(), F_OK|W_OK|R_OK) != 0) {
        if(mkfifo(strfifo.c_str(), 0660) != 0) {
            MYLOG_ERROR(CNFMain::g_pLogger, "mkfifo:%s failed, errno:%d.", strfifo.c_str(), errno);
            return CODE_FAILED;
        }
    }

    if((m_R.fd = open(strfifo.c_str(),(int)(O_RDWR|O_NONBLOCK))) < 0) {
        MYLOG_ERROR(CNFMain::g_pLogger, "FIFO_POINT open:%s for read failed, errno:%d.", strfifo.c_str(), errno);
        return CODE_FAILED;
    }
    AddFDToEpoll(m_R.fd, &m_R, EPOLLIN|EPOLLET);

    if(CreateThread(CNFThread::Thread, this, &m_ThreadID, 0, 0) != 0) {
        MYLOG_ERROR(CNFMain::g_pLogger, "create thread failed, errno:%d.", errno);
        return CODE_ERROR_CREATE_THREAD;
    }

    return CODE_OK;
}

int CNFThread::InitizlizeTimer(UINT32 timer_count)
{
    return m_Timers.InitializeLists(timer_count);
}

int CNFThread::Exit()
{
    if (close(m_EP) == -1) {
        MYLOG_ERROR(CNFMain::g_pLogger, "close fd:%d count:%d failed, errno:%d.", m_EP, m_FDCount, errno);
    }

    long retVal;
    void *ret = &retVal;
    pthread_join(m_ThreadID, &ret);

    MYLOG_INFO(CNFMain::g_pLogger, "thread:%s id:%u exited.", m_ThrName.c_str(), m_uID);
    return (int)retVal;
}

int CNFThread::ModFDToEpoll(int fd, void *p, int flag)
{
    if(m_FDCount >= MAX_CONNECTIONS_PER_THREAD) {
        MYLOG_ERROR(CNFMain::g_pLogger, "epoll_ctl add fd:%d p:%p failed, current fd count:%d>=%d.", fd, p, m_FDCount, MAX_CONNECTIONS_PER_THREAD);
        return CODE_FAILED;
    }

    int op;
    op = EPOLL_CTL_MOD;
    struct epoll_event ee;

    if(flag != 0) {
        ee.events = flag;
    } else {
        ee.events = EPOLLIN|m_Flag|EPOLLRDHUP;
    }

    if(p) {
        ee.data.ptr = p;
    } else {
        assert(0);
        return CODE_FAILED;
    }


    MYLOG_DEBUG(CNFMain::g_pLogger, "epoll mod event: flag:%x fd:%d count:%d p:%p.", flag, fd, m_FDCount, p);

    if (epoll_ctl(m_EP, op, fd, &ee) == -1) {
        MYLOG_WARN(CNFMain::g_pLogger, "epoll_ctl mod fd:%d count:%d p:%p failed, errno:%d.", fd, m_FDCount, p, errno);
        return CODE_FAILED;
    }
    return CODE_OK;
}

int CNFThread::AddFDToEpoll(int fd, void *p, int flag)
{
    if(m_FDCount >= MAX_CONNECTIONS_PER_THREAD) {
        MYLOG_ERROR(CNFMain::g_pLogger, "epoll_ctl add fd:%d p:%p failed, current fd count:%d>=%d.", fd, p, m_FDCount, MAX_CONNECTIONS_PER_THREAD);
        return CODE_FAILED;
    }

    int op;
    op = EPOLL_CTL_ADD;
    struct epoll_event ee;

    if(flag != 0) {
        ee.events = flag;
    } else {
        ee.events = EPOLLIN|m_Flag|EPOLLRDHUP;
    }

    if(p) {
        ee.data.ptr = p;
    } else {
        assert(0);
        return CODE_FAILED;
    }

    MYLOG_DEBUG(CNFMain::g_pLogger, "epoll add event: flag:%x fd:%d count:%d p:%p.", flag, fd, m_FDCount, p);

    if (epoll_ctl(m_EP, op, fd, &ee) == -1) {
        MYLOG_WARN(CNFMain::g_pLogger, "epoll_ctl add fd:%d count:%d p:%p failed, errno:%d.", fd, m_FDCount, p, errno);
        return CODE_FAILED;
    }
    ++m_FDCount;
    return CODE_OK;
}

int CNFThread::DelFDFromEpoll(int fd)
{
    int op;
    op = EPOLL_CTL_DEL;
    struct epoll_event ee;

    ee.events = 0;
    ee.data.ptr = NULL;

    MYLOG_DEBUG(CNFMain::g_pLogger, "epoll del fd:%d count:%d.", fd, m_FDCount);

    if (epoll_ctl(m_EP, op, fd, &ee) == -1) {
        MYLOG_WARN(CNFMain::g_pLogger, "epoll_ctl del fd:%d count:%d failed. errno:%d", fd, m_FDCount, errno);
        return CODE_FAILED;
    }

    --m_FDCount;
    if(m_FDCount < 0) {
        MYLOG_ERROR(CNFMain::g_pLogger, "epoll_ctl del fd:%d count:%d < 0. ", fd, m_FDCount);
    }
    return CODE_OK;
}


void CNFThread::Run()
{
    char buf[100];

    if(OnThreadStarted() != 0) {
        exit(0);
    }

    if(m_ListenD.fd >= 0) {
        AddFDToEpoll(m_ListenD.fd, &m_ListenD, 0);
    }

    while(CNFMain::bKeepRunning) {
        UINT32 wait_milisec = m_Timers.GetNearestTimeOut(NF_DEFAULT_WAIT_SECONDS);
        if(wait_milisec > (NF_DEFAULT_WAIT_SECONDS*1000)) {
            wait_milisec = NF_DEFAULT_WAIT_SECONDS*1000;
        }

        int events;
        events = epoll_wait(m_EP, &event_list[0], m_FDCount, wait_milisec);
        if (events == -1) {
            if(errno == EINTR) {
                MYLOG_DEBUG(CNFMain::g_pLogger, "epoll_wait() EINTRed wait_milisec:%u.", wait_milisec);
                continue;
            }
            MYLOG_DEBUG(CNFMain::g_pLogger, "epoll_wait() failed,errno:%d wait_milisec:%u.", errno, wait_milisec);
            return;
        }

        MYLOG_DEBUG(CNFMain::g_pLogger, "epoll_wait() return events:%d wait_milisec:%u.", events, wait_milisec);
        if (events == 0) {
            m_Timers.CheckTimeOuts();
            continue;
        }

        UINT32 i;
        UINT32 revents;
        bool bCheckCanAccept = true;

        for (i = 0; i < (UINT32)events; i++) {
            revents = event_list[i].events;
            if (revents & (EPOLLERR|EPOLLHUP)) {
                MYLOG_DEBUG(CNFMain::g_pLogger, "epoll_wait() error on fd:%d count:%d ev:%04XD", ((NF_EPDATA_HEAD *)(event_list[i].data.ptr))->fd, m_FDCount, revents);
            }

            if (revents & EPOLLIN) {
                if(((NF_EPDATA_HEAD *)(event_list[i].data.ptr))->fd == m_R.fd) {
                    // 本地消息
                    int recvLen = read(m_R.fd, buf, sizeof(buf));
                    while(recvLen > 0) {
                        recvLen = read(m_R.fd, buf, sizeof(buf));
                    }
                    assert(recvLen != 0);
                    CNFThread::DealMsgs();
                } else if (((NF_EPDATA_HEAD *)(event_list[i].data.ptr))->fd == m_ListenD.fd){
                    // 负载均衡，只有句柄数少的才accept
                    if(bCheckCanAccept) {
                        if(! m_pApp->CheckCanAccept(GetID(), m_FDCount)) {
                            bCheckCanAccept = false;
                            continue;
                        }
                    } else {
                        continue;
                    }

                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_sd;
                    client_sd = accept(((NF_EPDATA_HEAD *)(event_list[i].data.ptr))->fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_sd < 0) {
                        if((errno != EWOULDBLOCK) && (errno != EINTR)) {
                            MYLOG_WARN(CNFMain::g_pLogger, "accept error,fd:%d count:%d errno:%d.", ((NF_EPDATA_HEAD *)(event_list[i].data.ptr))->fd, m_FDCount, errno);
                        }
                    } else {
                        MYLOG_DEBUG(CNFMain::g_pLogger, "accept connection from %s:%d fd:%d listenfd:%d count:%d.", inet_ntoa(*(in_addr *)&client_addr.sin_addr.s_addr), ntohs(client_addr.sin_port), client_sd,m_ListenD.fd, m_FDCount);
                        SetAcceptSocketOpts(client_sd);
                        NF_EPDATA_HEAD * pHead = AllocateConnection(client_sd);
                        AddFDToEpoll(client_sd, pHead, 0);
                    }
                } else {
                    OnNetMsg(((NF_EPDATA_HEAD *)(event_list[i].data.ptr))->fd, revents, event_list[i].data.ptr);
                }

            } else if (revents & EPOLLOUT) {
                OnNetMsg(((NF_EPDATA_HEAD *)(event_list[i].data.ptr))->fd, revents, event_list[i].data.ptr);
                ModFDToEpoll(((NF_EPDATA_HEAD *)(event_list[i].data.ptr))->fd, event_list[i].data.ptr, 0);
            }
        }
        m_Timers.CheckTimeOuts();
    }
}

int CNFThread::PutMsg(char *pMsg)
{
    {
        CNFSafePLock Locker(&m_MsgsMutex);
        m_Msgs.push_back(pMsg);
    }

    int len = write(m_R.fd, "1", 1); // 表示有消息
    if(len < 1) {
        MYLOG_WARN(CNFMain::g_pLogger,"write have msg to thread:%d failed, errno:%d.",m_uID, errno);
        return CODE_FAILED;
    }

    return CODE_OK;
}


void CNFThread::DealMsgs()
{
    std::vector< char * > MsgList;
    {
        CNFSafePLock Locker(&m_MsgsMutex);
        MsgList.swap(m_Msgs);
    }
    std::vector< char * >::iterator itr;
    for(itr = MsgList.begin(); itr != MsgList.end(); ++itr) {
        OnLocalMsg(*itr);
        delete *itr;
    }
}

int CNFThread::SetupConnection(std::string strIP, UINT16 usPort, void *p)
{
    int fd;
    if((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        MYLOG_WARN(CNFMain::g_pLogger, "socket error, errno:%d.", errno);
        return CODE_FAILED;
    }
    SetConnectSocketOpts(fd);

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = PF_INET;
    addr.sin_port = htons(usPort);

    if (inet_pton(AF_INET, strIP.c_str(), (void *)&addr.sin_addr) <= 0) {
        MYLOG_WARN(CNFMain::g_pLogger, "setsockopt reuseaddr error, errno:%d.", errno);
        CLOSE_FD(fd);
        return CODE_FAILED;
    }

    if(connect(fd, (struct sockaddr*)(void *)&addr, sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            MYLOG_WARN(CNFMain::g_pLogger, "connect to %s:%d error, errno:%d.", strIP.c_str(), usPort, errno);
            CLOSE_FD(fd);
            return CODE_FAILED;
        }
    }

    ((NF_EPDATA_HEAD *)(p))->fd = fd;
    AddFDToEpoll(fd, p, EPOLLIN|EPOLLOUT|m_Flag|EPOLLRDHUP);

    return CODE_OK;
}

int CNFThread::SetTimer(UINT32 timerid, UINT32 par1, void *par2, unsigned long timeout, char tag)
{
    if(INVALID_UNIT_IDX == m_Timers.SetTimer(timerid, par1, par2, this, timeout, tag)) {
        MYLOG_ERROR(CNFMain::g_pLogger, "thread:%s id:%d SetTimer timerid:%u failed.", m_ThrName.c_str(), m_uID, timerid);
        return CODE_FAILED;
    } else {
        return CODE_OK;
    }
}

void *CNFThread::Thread(void *thr)
{
    CNFThread *Thr = (CNFThread *)thr;
    assert(Thr);
    Thr->Run();
    return (void*)0;
}

int CNFThread::SetConnectSocketOpts(int socket)
{
    int flags;

    if((flags = fcntl(socket, F_GETFL, 0)) < 0) {
        MYLOG_WARN(CNFMain::g_pLogger, "socket:%d F_GETFL failed. errno:%d.", socket, errno);
        return -1;
    }

    if(fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        MYLOG_WARN(CNFMain::g_pLogger, "socket:%d F_SETFL failed. errno:%d.", socket, errno);
        return -1;
    }

    MYLOG_DEBUG(CNFMain::g_pLogger, "socket:%d setted to nonblock.", socket);

    int reuseaddr;
    reuseaddr = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,(const void *) &reuseaddr, sizeof(int))== -1) {
        MYLOG_WARN(CNFMain::g_pLogger, "socket:%d setsockopt(SO_REUSEADDR) failed. errno:%d.", socket, errno);
        return -1;
    }
    MYLOG_DEBUG(CNFMain::g_pLogger, "socket:%d setsockopt(SO_REUSEADDR) success.", socket);

    return 0;
}


int CNFThread::SetAcceptSocketOpts(int socket)
{
    int flags;

    if((flags = fcntl(socket, F_GETFL, 0)) < 0) {
        MYLOG_WARN(CNFMain::g_pLogger, "socket:%d F_GETFL failed. errno:%d.", socket, errno);
        return -1;
    }

    if(fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        MYLOG_WARN(CNFMain::g_pLogger, "socket:%d F_SETFL failed. errno:%d.", socket, errno);
        return -1;
    }

    if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF,(const void *) &m_TcpRecvBufSize, sizeof(int))== -1) {
        MYLOG_WARN(CNFMain::g_pLogger, "socket:%d setsockopt(SO_RCVBUF) failed. errno:%d.", socket, errno);
    }

    if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF,(const void *) &m_TcpSndBufSize, sizeof(int))== -1) {
        MYLOG_WARN(CNFMain::g_pLogger, "socket:%d setsockopt(SO_SNDBUF) failed. errno:%d.", socket, errno);
    }

    if(m_TcpKeepAlive) {
        // 开启keepalive属性
        flags = 1;
        if (setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE,(const void *) &flags, sizeof(int))== -1) {
            MYLOG_WARN(CNFMain::g_pLogger, "socket:%d setsockopt(SO_KEEPALIVE) failed. errno:%d.", socket, errno);
        }

        // 如该连接在60秒内没有任何数据往来,则进行探测
        flags = 60;
        if (setsockopt(socket, IPPROTO_TCP, TCP_KEEPIDLE,(const void *) &flags, sizeof(int))== -1) {
            MYLOG_WARN(CNFMain::g_pLogger, "socket:%d setsockopt(TCP_KEEPIDLE) failed. errno:%d.", socket, errno);
        }

        // 探测时发包的时间间隔为5 秒
        flags = 5;
        if (setsockopt(socket, IPPROTO_TCP, TCP_KEEPINTVL,(const void *) &flags, sizeof(int))== -1) {
            MYLOG_WARN(CNFMain::g_pLogger, "socket:%d setsockopt(TCP_KEEPINTVL) failed. errno:%d.", socket, errno);
        }

        // 探测尝试的次数.如果第1次探测包就收到响应了,则后2次的不再发.
        flags = 3;
        if (setsockopt(socket, IPPROTO_TCP, TCP_KEEPCNT,(const void *)&flags, sizeof(int))== -1) {
            MYLOG_WARN(CNFMain::g_pLogger, "socket:%d setsockopt(TCP_KEEPCNT) failed. errno:%d.", socket, errno);
        }
    }

    return 0;
}
