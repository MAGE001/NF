#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include "NFMain.h"
#include "NFThread.h"

volatile bool CNFMain::bKeepRunning = true;
log4cxx::LoggerPtr CNFMain::g_pLogger;

CNFMain::CNFMain()
{
}

CNFMain::~CNFMain()
{
}

int CNFMain::Init()
{
    if (g_pLogger == 0) {
        g_pLogger=log4cxx::Logger::getRootLogger();
    }

    // https://stackoverflow.com/questions/23889062/c-how-to-handle-sigpipe-in-a-multithreaded-environment
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &signals, 0);

    m_LastMinFDCount = 0;
    m_LastMaxFDCount = NF_FD_ALLOWED_GAP;
    return 0;
}


/*
  窗口是 [m_LastMinFDCount m_LastMaxFDCount] ，m_LastMaxFDCount-m_LastMinFDCount一直维持NF_FD_ALLOWED_GAP
*/
bool CNFMain::CheckCanAccept(UINT32 id, int val)
{
    // 每个线程最大支持10W连接
    if(m_Threads[id]->m_FDCount >= MAX_CONNECTIONS_PER_THREAD) {
        MYLOG_INFO(g_pLogger, "thread:%d fdcount:%d > %d. withdraw, do not accept.", id, m_Threads[id]->m_FDCount, MAX_CONNECTIONS_PER_THREAD);
        return false;
    }

    if(!(m_Threads[id]->m_UseBalance)) {
        return true;
    }

    if(val > m_LastMaxFDCount) {
        MYLOG_INFO(g_pLogger, "thread:%d fdcount:%d > %d. withdraw, do not accept.", id, m_Threads[id]->m_FDCount, m_LastMaxFDCount);
        return false;
    }


    if(val < m_LastMinFDCount) {
        //小到一定程度，把窗口回收
        if(val < (m_LastMinFDCount - NF_FD_ALLOWED_GAP)) {
            m_LastMinFDCount = val + 1;
            m_LastMaxFDCount = m_LastMinFDCount + NF_FD_ALLOWED_GAP;
        }
        return true;
    }

    m_LastMinFDCount = val + 1;
    m_LastMaxFDCount = m_LastMinFDCount + NF_FD_ALLOWED_GAP;
    return true;
}


int CNFMain::AddThread(CNFThread *pThr, const char *pName, int flag, UINT32 timer_count)
{
    if(!pThr) {
        MYLOG_ERROR(g_pLogger, "AddThread , null parameter.");
        return CODE_ERROR_PARAM;
    }

    if((flag != 0) && (flag != (int)EPOLLET)) {
        MYLOG_ERROR(g_pLogger, "AddThread , invalid flag:%x.", flag);
        return CODE_ERROR_PARAM;
    }

    m_Threads.push_back(pThr);

    pThr->SetID(m_Threads.size() - 1);
    pThr->SetThrName(pName);
    pThr->SetApp(this);
    pThr->SetFlag(flag);

    return pThr->InitizlizeTimer(timer_count);
}


int CNFMain::ListenIPPort(const std::string & strIP, UINT16 usPort, int backlog)
{
    int listenfd;
    if((listenfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        MYLOG_WARN(g_pLogger, "socket error, errno:%d.",errno);
        return -1;
    }

    int flags;
    if((flags = fcntl(listenfd, F_GETFL, 0)) < 0) {
        MYLOG_WARN(CNFMain::g_pLogger, "socket:%d F_GETFL failed. errno:%d.", listenfd, errno);
        return -1;
    }
    if(fcntl(listenfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        MYLOG_WARN(CNFMain::g_pLogger, "socket:%d F_SETFL failed. errno:%d.", listenfd, errno);
        return -1;
    }

    // http://www.delorie.com/gnu/docs/glibc/libc_352.html
    struct linger optval = {0, 0};
    if (setsockopt(listenfd, SOL_SOCKET, SO_LINGER, (char * )&optval, sizeof(optval))) {
        MYLOG_WARN(g_pLogger, "setsockopt linger error, errno:%d.", errno);
        CLOSE_FD(listenfd);
        return -1;
    }

    const int one = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one))) {
        MYLOG_WARN(g_pLogger, "setsockopt reuseaddr error, errno:%d.", errno);
        CLOSE_FD(listenfd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = PF_INET;
    addr.sin_port = htons(usPort);

    if (inet_pton(AF_INET, strIP.c_str(), (void *)&addr.sin_addr) <= 0) {
        MYLOG_WARN(g_pLogger, "setsockopt reuseaddr error, errno:%d.", errno);
        CLOSE_FD(listenfd);
        return -1;
    }

    if(bind(listenfd, (struct sockaddr *)(void *)&addr, sizeof(addr)) != 0) {
        MYLOG_WARN(g_pLogger, "bind %s:%d error, errno:%d.", strIP.c_str(), usPort, errno);
        CLOSE_FD(listenfd);
        return -1;
    }

    if (listen(listenfd, backlog) != 0) {
        MYLOG_WARN(g_pLogger, "listen %s:%d error, errno:%d.", strIP.c_str(), usPort, errno);
        CLOSE_FD(listenfd);
        return -1;
    }

    return listenfd;
}


void CNFMain::Run(const std::string & strIP, UINT16 usPort, UINT32 fd_count, int backlog)
{
    int ret;
    int listenfd = -1;
    Init();

    // 如果提供strIP参数，则作为Server
    if(strIP.size() > 0) {
        listenfd = ListenIPPort(strIP, usPort, backlog);
        if(listenfd < 0) {
            return;
        }
    }

    std::vector<CNFThread *>::iterator iter;
    for (iter = m_Threads.begin(); iter != m_Threads.end(); iter++) {
        ret = (*iter)->InitizlizeThread(fd_count);
        if(CODE_OK != ret) {
            MYLOG_ERROR(g_pLogger, "thread:%u Init failed, return:%d.", (*iter)->GetID(), ret);
            return;
        }
        (*iter)->SetListenFD(listenfd);
    }

    while(bKeepRunning) {
        sleep(1);
    }

    for (iter = m_Threads.begin(); iter != m_Threads.end(); iter++) {
        (*iter)->Exit();
    }
}
