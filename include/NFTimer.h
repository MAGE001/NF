#ifndef __NF_TIMER_H__
#define __NF_TIMER_H__

//
//    FileName: NFTimer.h
// Description：定时器
//

#include <sys/time.h>

#include "NFGlobal.h"
#include "NFTypedef.h"
#include "NFLocker.h"

#define NF_MAX_TIMEOUT  86400000 // 最多一天
#define NF_MAX_TIME_UNIT  100000 // must >= 2
#define INVALID_UNIT_IDX  0xFFFFFFFF
#define INVALID_LIST_IDX  0xFFFFFFFF
#define INVALID_TIMMER_ID  INVALID_UNIT_IDX
#define TIMER_LIST_SPLITS 86400
#define NF_TIMER_SLEEP_TIME  200000  //must < 1000000
#define TIMER_TYPE_ONECE 0
#define TIMER_TYPE_PERMENENT 1

class CNFThread;

struct NF_TIME_UNIT
{
    UINT32 timerid;
    UINT32 parm1;
    void *parm2;
    CNFThread *phandler;
    struct timeval tnext;
    UINT32 nextidx;
    UINT32 previdx;
    UINT32 curlst;
    UINT64 timeout;
    UINT64 tag;

    void Init(UINT32 iPrev, UINT32 iNext, UINT32 iSelf)
    {
        timerid = parm1 = 0;
        parm2 = 0;
        phandler = 0;
        curlst = INVALID_LIST_IDX;
        previdx = iPrev;
        nextidx = iNext;
        tag = 0;
    }

    int SetTimer(UINT32 tid, UINT32 par1, void *par2,
                 CNFThread *ph, unsigned long tout, char bperm, const timeval &tNow)
    {
        if(tout > NF_MAX_TIMEOUT) {
            return CODE_ERROR_PARAM;
        }

        timerid = tid;
        parm1 = par1;
        parm2 = par2;
        phandler = ph;
        timeout = tout;
        tag = bperm;

        time_t tAdd = (time_t)0;
        tnext.tv_usec = (timeout % 1000) * 1000 +  tNow.tv_usec;
        if(tnext.tv_usec > 1000000) {
            tnext.tv_usec -= 1000000;
            tAdd = (time_t)1;
        }
        tnext.tv_sec = (timeout / 1000) + tAdd + tNow.tv_sec;
        return CODE_OK;
    }

    void SetNextTimeOut(const timeval &tNow)
    {
        time_t tAdd = (time_t)0;
        tnext.tv_usec = (timeout % 1000) * 1000 +  tNow.tv_usec;
        if(tnext.tv_usec > 1000000) {
            tnext.tv_usec -= 1000000;
            tAdd = (time_t)1;
        }
        tnext.tv_sec = (timeout / 1000) + tAdd + tNow.tv_sec;
    }
};


class CNFTimer
{
 private:
    NF_TIME_UNIT *m_TimeOuts;
    UINT32 m_UnitCount;
    UINT32 m_FreeIdx[2];  // [0] is head, [1] is tail

    // 双向链表
    UINT32 m_TLists[TIMER_LIST_SPLITS][2];  // [0] is head, [1] is tail

    pthread_mutex_t m_Mutex;
    timeval m_tNow;
    timeval m_tLast;

    void FreeTimeUnit(UINT32 idx);
    UINT32 GetTimeUnit();
    void InsertListUnit(UINT32 listidx, UINT32 timeidx);
    void RemoveListUnit(UINT32 listidx, UINT32 timeidx);
    void DelListUnit(UINT32 listidx, UINT32 timeidx)
    {
        RemoveListUnit(listidx, timeidx);
        FreeTimeUnit(timeidx);
    }

    void DoTimeOut(UINT32 Idx,UINT32 listidx);
    void CheckTimeOut(UINT32 listidx);

 public:
    CNFTimer();
    virtual ~CNFTimer();
    int InitializeLists(UINT32 unit_count = NF_MAX_TIME_UNIT);

    UINT32 GetNearestTimeOut(UINT32 in_seconds);
    UINT32 SetTimer(UINT32 timerid, UINT32 par1, void *par2, CNFThread *ph, unsigned long timeout, char tag);
    void KillTimer(UINT32 tid);
    int CheckTimeOuts();
};

#endif

