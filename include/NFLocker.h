#ifndef __NF_LOCKER_H__
#define __NF_LOCKER_H__

//
//    FileName: NFLocker.h
// Description：互斥锁，读写锁，事件通知，事件广播
//

#include <pthread.h>
#include <errno.h>

class CNFSafePLock
{
 private:
    pthread_mutex_t *m_pMutex;

 public:
    CNFSafePLock(pthread_mutex_t *plock) {
        m_pMutex = plock;
        if(m_pMutex) {
            pthread_mutex_lock(m_pMutex);
        }
    }

    ~CNFSafePLock() {
        if(m_pMutex) {
            pthread_mutex_unlock(m_pMutex);
        }
    }
};

class CNFSafeAntiPLock
{
 private:
    pthread_mutex_t *m_pMutex;

 public:
    CNFSafeAntiPLock(pthread_mutex_t *plock) {
        m_pMutex = plock;
        if(m_pMutex) {
            pthread_mutex_unlock(m_pMutex);
        }
    }

    ~CNFSafeAntiPLock() {
        if(m_pMutex) {
            pthread_mutex_lock(m_pMutex);
        }
    }
};

class CNFSafeRWLock
{
 private:
    pthread_rwlock_t * m_pRwLock;

 public:
    CNFSafeRWLock(pthread_rwlock_t * pRwLock, bool bWriteLock) {
        m_pRwLock = pRwLock;
        if(m_pRwLock) {
            if (bWriteLock) {
                pthread_rwlock_wrlock(m_pRwLock);
            } else {
                pthread_rwlock_rdlock(m_pRwLock);
            }
        }
    }

    ~CNFSafeRWLock() {
        if(m_pRwLock) {
            pthread_rwlock_unlock(m_pRwLock);
        }
    }
};

class CNFNotifyEvent
{
 private:
    pthread_mutex_t lk;
    pthread_cond_t cd;

    //0 没有事件; 1 有事件
    int flag;
 public:
    enum WAIT_TIME
    {
        WAIT_INFINITE = 0xFFFFFFFF,
    };
    enum WAIT_RETURN
    {
        RETURN_EVENT = 0,
        RETURN_TIMEOUT = 1,
        RETURN_FAILD = 0xFFFFFFFF,
    };

    CNFNotifyEvent() {
        pthread_mutex_init(&lk, NULL);
        pthread_cond_init(&cd, NULL);
        flag = 0;
    }
    ~CNFNotifyEvent() {
        pthread_mutex_destroy(&lk);
        pthread_cond_destroy(&cd);
    }

    void Notify()
    {
        pthread_mutex_lock(&lk);
        flag = 1;
        pthread_cond_signal(&cd);
        pthread_mutex_unlock(&lk);
    }

    void Reset()
    {
        pthread_mutex_lock(&lk);
        flag = 0;
        pthread_mutex_unlock(&lk);
    }

    int WaitEvent(unsigned long milliseconds)
    {
        int ret;
        if (milliseconds == WAIT_INFINITE) {
            ret = pthread_mutex_lock(&lk);
            if(flag == 1) {
                flag = 0;
                ret = pthread_mutex_unlock(&lk);
                return (int)RETURN_EVENT;
            }

            ret = pthread_cond_wait(&cd, &lk);
            if(flag == 1) {
                flag = 0;
                ret = pthread_mutex_unlock(&lk);
                return (int)RETURN_EVENT;
            }

            ret = pthread_mutex_unlock(&lk);
            return (int)RETURN_FAILD;
        } else { //or milliseconds
            pthread_mutex_lock(&lk);
            if(flag == 1) {
                flag = 0;
                ret = pthread_mutex_unlock(&lk);
                return (int)RETURN_EVENT;
            }

            struct timespec nxttime;
            clock_gettime(CLOCK_REALTIME,&nxttime);
            nxttime.tv_sec += milliseconds / 1000;
            nxttime.tv_nsec += (milliseconds % 1000) * 1000000;

            while (nxttime.tv_nsec >= 1000000000) {
                nxttime.tv_sec++;
                nxttime.tv_nsec -= 1000000000;
            }

            ret = pthread_cond_timedwait(&cd, &lk, &nxttime);
            if(flag == 1) {
                flag = 0;
                ret = pthread_mutex_unlock(&lk);
                return (int)RETURN_EVENT;
            }

            pthread_mutex_unlock(&lk);

            //退出
            switch(ret) {
            case ETIMEDOUT:
                return (int)RETURN_TIMEOUT;
            default:
                return (int)RETURN_FAILD;
            }
        }
    }
};


class CNFBroadCastEvent
{
 private:
    pthread_mutex_t    lk;
    pthread_cond_t      cd;
 public:
    CNFBroadCastEvent() {
        pthread_mutex_init(&lk, NULL);
        pthread_cond_init(&cd, NULL);
    }
    ~CNFBroadCastEvent() {
        pthread_mutex_destroy(&lk);
        pthread_cond_destroy(&cd);
    }

    void Notify()
    {
        pthread_mutex_lock(&lk);
        pthread_cond_broadcast(&cd);
        pthread_mutex_unlock(&lk);
    }

    int WaitEvent(unsigned long milliseconds)
    {
        pthread_mutex_lock(&lk);
        struct timespec nxttime;
        clock_gettime(CLOCK_REALTIME,&nxttime);
        nxttime.tv_sec += milliseconds / 1000;
        nxttime.tv_nsec += (milliseconds % 1000) * 1000000;

        while (nxttime.tv_nsec >= 1000000000) {
            nxttime.tv_sec++;
            nxttime.tv_nsec -= 1000000000;
        }

        int ret = pthread_cond_timedwait(&cd, &lk, &nxttime);
        pthread_mutex_unlock(&lk);
        return ret;
    }
};

#endif

