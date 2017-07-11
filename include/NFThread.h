#ifndef __NF_THREAD_H__
#define __NF_THREAD_H__

#include <string>
#include "NFTypedef.h"
#include "NFMessage.h"
#include "NFTimer.h"

#define MAX_CONNECTIONS_PER_THREAD 100000
#define NF_DISPATCH_MSG(msg, usRecv) m_pApp->m_Threads[usRecv]->PutMsg(msg)
class CNFMain;

//Every NFThread will open a fifo for getting msg from other thread.
class CNFThread
{
 private:
    UINT32 m_uID;
    UINT32 m_UseBalance;
    pthread_t m_ThreadID;
    std::string m_ThrName;

    int m_TcpRecvBufSize;
    int m_TcpSndBufSize;
    int m_TcpKeepAlive;

    pthread_mutex_t m_MsgsMutex;
    std::vector< char * > m_Msgs;

    struct epoll_event *event_list;
    int m_EP;
    int m_FDCount;
    NF_EPDATA_HEAD m_R;

    //epoll listen flag,  can be 0 or EPOLLET
    int m_Flag;

    NF_EPDATA_HEAD m_ListenD;
    CNFTimer m_Timers;

    static void *Thread(void *thr);
    virtual void Run();
    void DealMsgs();

    friend class CNFMain;

    int InitizlizeThread(int fd_count);
    int InitizlizeTimer(UINT32 timer_count = NF_MAX_TIME_UNIT);

    void SetID(UINT32 taskid) {
        m_uID = taskid;
    }
    void SetThrName(const char *pName) {
        m_ThrName = pName;
    }
    void SetApp(CNFMain *pApp) {
        m_pApp = pApp;
    }

    void SetFlag(int flag) {
        m_Flag = flag;
    }

    int Exit();
 protected:
    CNFMain *m_pApp;

    int AddFDToEpoll(int fd, void *p, int flag = 0);
    int ModFDToEpoll(int fd, void *p, int flag = 0);
    int DelFDFromEpoll(int fd);
    int SetConnectSocketOpts(int socket);
    int SetAcceptSocketOpts(int socket);
    int SetupConnection(std::string strIP, UINT16 usPort, void *p);
    int SetTimer(UINT32 timerid, UINT32 par1, void *par2, unsigned long timeout, char tag);

    int GetFDCount() {
        return m_FDCount;
    }

    void SetTcpSndBufSize(int size) {
        m_TcpSndBufSize = size;
    }

    void SetTcpRecvBufSize(int size) {
        m_TcpRecvBufSize = size;
    }

    void SetTcpKeepAlive() {
        m_TcpKeepAlive = 1;
    }

    virtual NF_EPDATA_HEAD * AllocateConnection(int fd) {
        assert(0);
        return 0;
    }
    virtual void ReleaseConnection(void *p) {
        assert(0);
        return;
    }

    char *AllocateMsgToOtherThread(UINT32 size, UINT16 dest, UINT32 msgtype) {
        assert (size >= sizeof(NF_MSG_HEAD));

        char *pMsg = new char[size];
        NF_MSG_HEAD *pHead = (NF_MSG_HEAD *)pMsg;
        pHead->ulMsgSize = size;
        pHead->ulMsgType = msgtype;
        pHead->usSend = GetID();
        pHead->usRecv = dest;
        return pMsg;
    }

    void SetListenFD(int fd) {
        m_ListenD.fd = fd;
    }

 public:
    CNFThread();
    virtual ~CNFThread();

    UINT32 GetID() {
        return m_uID;
    }
    const std::string &GetThrName() {
        return m_ThrName;
    }
    void SetUseBalance() {
        m_UseBalance = 1;
    }

    // thread already started.  for initializing something
    virtual int OnThreadStarted() = 0;

    // time out function.
    virtual void OnTimer(UINT32 timerid, UINT32 par1, void *par2) = 0;

    // get something from net
    virtual void OnNetMsg(int fd, UINT32 revents, void *handler) = 0;

    // get something from local
    virtual void OnLocalMsg(char *pMsg) = 0;

    // for other local modules put msg to this thread.
    int PutMsg( char * pMsg);
};

#endif

