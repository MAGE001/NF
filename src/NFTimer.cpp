#include <stdlib.h>
#include "NFTimer.h"
#include "NFMain.h"
#include "NFThread.h"

CNFTimer::CNFTimer()
{
    m_UnitCount = 0;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE);
    pthread_mutex_init(&m_Mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

CNFTimer::~CNFTimer()
{
    long retVal;
    void *ret = &retVal;
    ret=ret;
    pthread_mutex_destroy(&m_Mutex);
}

int CNFTimer::InitializeLists(UINT32 unit_count)
{
    struct timezone tz;
    if(gettimeofday(&m_tNow,&tz) != 0) {
        return CODE_ERROR_SYSCALL;
    }
    if(unit_count < 2) {
        unit_count = 2;
    }
    m_UnitCount = unit_count;

    NEWARRAY(m_TimeOuts, NF_TIME_UNIT, m_UnitCount);
    if(NULL == m_TimeOuts) {
        return CODE_NEW_FAILED;
    }


    m_FreeIdx[0] = 0;
    m_FreeIdx[1] = m_UnitCount - 1;
    m_TimeOuts[0].Init(INVALID_UNIT_IDX, 1, 0);

    UINT32 i;
    for(i = 1; i < m_UnitCount -1; ++i) {
        m_TimeOuts[i].Init(i-1, i + 1, i);
    }

    m_TimeOuts[m_UnitCount - 1].Init(m_UnitCount - 2, INVALID_UNIT_IDX,m_UnitCount - 1);

    for(i = 0; i < TIMER_LIST_SPLITS; ++i) {
        m_TLists[i][0] = INVALID_UNIT_IDX;
        m_TLists[i][1] = INVALID_UNIT_IDX;
    }
    return CODE_OK;
}

int CNFTimer::CheckTimeOuts()
{
    struct timezone tz;

    m_tLast = m_tNow;
    if(gettimeofday(&m_tNow,&tz) != 0) {
        return CODE_FAILED;
    }

    UINT32 from, to, cir;
    from = m_tLast.tv_sec%TIMER_LIST_SPLITS;
    to = m_tNow.tv_sec%TIMER_LIST_SPLITS;

    //如果跨越多个时间片
    if(to >= from) {
        for(cir = from; cir<= to; ++cir) {
            CheckTimeOut(cir);
        }
    } else {
        for(cir = from; cir < TIMER_LIST_SPLITS; ++cir) {
            CheckTimeOut(cir);
        }
        for(cir = 0; cir<= to; ++cir) {
            CheckTimeOut(cir);
        }
    }

    return CODE_OK;
}


void CNFTimer::FreeTimeUnit(UINT32 idx)
{
    //插到头部
    m_TimeOuts[idx].nextidx = m_FreeIdx[0];
    m_TimeOuts[idx].previdx = INVALID_UNIT_IDX;
    m_TimeOuts[idx].curlst = INVALID_LIST_IDX;

    //如果队列为空
    if(m_FreeIdx[0] == INVALID_UNIT_IDX) {
        m_FreeIdx[0] = idx;
        m_FreeIdx[1] = idx;
    } else {
        m_TimeOuts[m_FreeIdx[0]].previdx = idx;
        m_FreeIdx[0] = idx;
    }
}

UINT32 CNFTimer::GetTimeUnit()
{
    if(m_FreeIdx[0] >= m_UnitCount) {
        return INVALID_UNIT_IDX;
    }

    //从头部取
    UINT32 ret = m_FreeIdx[0];

    //如果队列中只有一个Unit
    if(m_FreeIdx[1] == ret) {
        m_FreeIdx[1] = INVALID_UNIT_IDX;
        m_FreeIdx[0] = INVALID_UNIT_IDX;
    } else {
        m_FreeIdx[0] = m_TimeOuts[ret].nextidx;
        m_TimeOuts[m_FreeIdx[0]].previdx = INVALID_UNIT_IDX;
    }

    //清空Unit
    m_TimeOuts[ret].previdx = INVALID_UNIT_IDX;
    m_TimeOuts[ret].nextidx = INVALID_UNIT_IDX;
    return ret;
}

void CNFTimer::RemoveListUnit(UINT32 listidx, UINT32 timeidx)
{
    if(m_TimeOuts[timeidx].curlst  != listidx) {
        assert(0);
    }

    if(m_TLists[listidx][0] == INVALID_UNIT_IDX) {
        assert(0);
    }

    UINT32 iP, iN;
    iP = m_TimeOuts[timeidx].previdx;
    iN = m_TimeOuts[timeidx].nextidx;

    if(iP != INVALID_UNIT_IDX) {
        m_TimeOuts[iP].nextidx = iN;
    } else {
        //首元素被删除
        m_TLists[listidx][0] = iN;
    }

    if(iN != INVALID_UNIT_IDX) {
        m_TimeOuts[iN].previdx = iP;
    } else {
        //尾元素被删除
        m_TLists[listidx][1] = iP;
    }
}

void CNFTimer::InsertListUnit(UINT32 listidx, UINT32 timeidx)
{
    //加到尾部
    m_TimeOuts[timeidx].previdx= m_TLists[listidx][1];
    m_TimeOuts[timeidx].nextidx= INVALID_UNIT_IDX;
    m_TimeOuts[timeidx].curlst = listidx;

    //如果队列为空
    if(m_TLists[listidx][0] == INVALID_UNIT_IDX) {
        m_TLists[listidx][0] = timeidx;
        m_TLists[listidx][1] = timeidx;
    } else {
        m_TimeOuts[m_TLists[listidx][1]].nextidx = timeidx;
        m_TLists[listidx][1] = timeidx;
    }

}

UINT32 CNFTimer::GetNearestTimeOut(UINT32 in_seconds)
{
    UINT32 from, to, cir, diff;
    UINT32 iH = INVALID_UNIT_IDX;
    from = m_tNow.tv_sec%TIMER_LIST_SPLITS;
    to = (from + in_seconds) %TIMER_LIST_SPLITS;

    if(to >= from) {
        for(cir = from; cir<= to; ++cir) {
            iH = m_TLists[cir][0];
            if(iH == INVALID_UNIT_IDX) {
                continue;
            }
            diff =  (cir-from) *1000;
            break;
        }
    } else {
        for(cir = from; cir< TIMER_LIST_SPLITS; ++cir) {
            iH = m_TLists[cir][0];
            if(iH == INVALID_UNIT_IDX) {
                continue;
            }
            diff =  (cir-from) *1000;
            break;
        }
        if(iH == INVALID_UNIT_IDX) {
            for(cir = 0; cir<= to; ++cir) {
                iH = m_TLists[cir][0];
                if(iH == INVALID_UNIT_IDX) {
                    continue;
                }
                diff =  cir *1000;
                break;
            }
        }
    }

    if(iH != INVALID_UNIT_IDX) {
        if(m_TimeOuts[iH].tnext.tv_usec > m_tNow.tv_usec) {
            diff += (m_TimeOuts[iH].tnext.tv_usec - m_tNow.tv_usec)/1000 + 1;
        } else {
            if(diff >= 1000) {
                diff -= 1000;
                diff += (1000000 + (m_tNow.tv_usec - m_TimeOuts[iH].tnext.tv_usec))/1000 + 1;
            } else {
                diff = 500;
            }
        }
        return diff;
    }

    return NF_MAX_TIMEOUT;
}


UINT32 CNFTimer::SetTimer(UINT32 timerid, UINT32 par1, void *par2, CNFThread *ph, unsigned long timeout, char tag)
{
    UINT32 idx = GetTimeUnit();
    if(idx == INVALID_UNIT_IDX) {
        return idx;
    }

    if(m_TimeOuts[idx].SetTimer(timerid, par1, par2, ph, timeout,tag, m_tNow) != CODE_OK) {
        FreeTimeUnit(idx);
        return INVALID_UNIT_IDX;
    }

    UINT32 lstidx = m_TimeOuts[idx].tnext.tv_sec % TIMER_LIST_SPLITS;

    InsertListUnit(lstidx, idx);

    return idx;
}


void CNFTimer::KillTimer(UINT32 tid)
{
    //可能已经被回收了
    if(m_TimeOuts[tid].curlst >= TIMER_LIST_SPLITS) {
        return;
    }

    DelListUnit(m_TimeOuts[tid].curlst, tid);
}

void CNFTimer::DoTimeOut(UINT32 Idx,UINT32 listidx)
{
    m_TimeOuts[Idx].phandler->OnTimer(m_TimeOuts[Idx].timerid, m_TimeOuts[Idx].parm1, m_TimeOuts[Idx].parm2);
    if(! m_TimeOuts[Idx].tag) {
        DelListUnit(listidx, Idx);
    } else {
        m_TimeOuts[Idx].SetNextTimeOut(m_tNow);

        UINT32 lstnew = m_TimeOuts[Idx].tnext.tv_sec % TIMER_LIST_SPLITS;
        if(listidx != lstnew) {
            RemoveListUnit(listidx, Idx);
            InsertListUnit(lstnew, Idx);
        }
    }
}

void CNFTimer::CheckTimeOut(UINT32 listidx)
{
    UINT32 iH = m_TLists[listidx][0];
    if(iH == INVALID_UNIT_IDX) {
        return;
    }

    UINT32 Idx = iH;
    UINT32 IdxNext = Idx;
    while(IdxNext != INVALID_UNIT_IDX) {
        IdxNext = m_TimeOuts[Idx].nextidx;

        if(m_tNow.tv_sec > m_TimeOuts[Idx].tnext.tv_sec) {
            DoTimeOut(Idx, listidx);
        } else if(m_tNow.tv_sec == m_TimeOuts[Idx].tnext.tv_sec) {
            if(m_tNow.tv_usec >= m_TimeOuts[Idx].tnext.tv_usec) {
                DoTimeOut(Idx, listidx);
            }
        }

        Idx = IdxNext;
    }
}
