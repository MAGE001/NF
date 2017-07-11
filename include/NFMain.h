#ifndef __NF_MAIN_H__
#define __NF_MAIN_H__

#include <vector>
#include <string>
#include <sys/epoll.h>
#include "NFThread.h"
#include "NFTypedef.h"
#include "Log.h"

typedef std::vector<CNFThread *> NFThreadVector;

class CNFMain
{
private:

    std::string m_ModuleName;
    int m_LastMaxFDCount;
    int m_LastMinFDCount;

    int Init();
public:
    static volatile bool bKeepRunning;
    static log4cxx::LoggerPtr g_pLogger;
    NFThreadVector m_Threads;

    bool CheckCanAccept(UINT32 id, int val);
public:
    CNFMain();
    virtual ~CNFMain();

    void SetModuleName(const std::string & strModule)
    {
        m_ModuleName = strModule;
    }

    int ListenIPPort(const std::string & strIP, UINT16 usPort, int backlog);
    int AddThread(CNFThread *pThr, const char *pName, int flag=EPOLLET, UINT32 timer_count = 2);
    void Run(const std::string & strIP, UINT16 usPort, UINT32 fd_count = 100000, int backlog = 10);
};

#endif
