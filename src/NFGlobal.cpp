#include <sys/socket.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "NFGlobal.h"

int CreateThread(POSIX_THREAD_FUNC func, void *Param, pthread_t *thr_id, long priority, size_t stacksize)
{
    int result;
    pthread_attr_t attr;
    if (::pthread_attr_init (&attr) != 0) {
            return -1;
        }

    if (stacksize == 0) {
            stacksize = NF_DEFAULT_STACK_SIZE;
        }

    if (::pthread_attr_setstacksize (&attr, stacksize) != 0) {
            ::pthread_attr_destroy (&attr);
            return -1;
        }

    result = ::pthread_create (thr_id, &attr, func, Param);
    ::pthread_attr_destroy (&attr);

    return result;
}


int CheckAndMakeDirs(const char *pszDir, int *pret)
{
    const char *p;
    *pret = 0;

    size_t sz;
    sz = strlen(pszDir);

    p = strchr(pszDir, '/');
    while(p!=NULL) {
        // 取到 '/'前的字符串
        std::string dir(pszDir, p-pszDir);
        if(dir.size() > 0) {
            struct stat st;
            if (stat(dir.c_str(), &st) || !S_ISDIR(st.st_mode)) {
                unlink(dir.c_str());
                if (mkdir(dir.c_str(), 0755)) {
                    *pret = errno;
                    return CODE_FAILED;
                }
            }
        }

        // //xxx/yyy/形式
        if((size_t)(p + 1 - pszDir) == sz) {
            return CODE_OK;
        }

        p = strchr(p+1, '/');
    }

    // /xxx/yyy形式
    std::string dir(pszDir, sz);
    if(dir.size() > 0) {
        struct stat st;
        if (stat(dir.c_str(), &st) || !S_ISDIR(st.st_mode)) {
            unlink(dir.c_str());
            if (mkdir(dir.c_str(), 0755)) {
                *pret = errno;
                return CODE_FAILED;
            }
            return CODE_OK;
        } else {
            return CODE_OK;
        }
    }

    return CODE_FAILED;
}

void GetProcessName(std::string &str,std::string &strPath)
{
    char szName[512] = {0};
    char *p = NULL;
    int len = readlink("/proc/self/exe", szName, sizeof(szName));
    if(len == -1) {
        strcpy(szName, "prog");
    } else {
        p = strrchr(szName, '/');
        if(p) {
            p++;
        } else {
            p = szName;
        }
    }
    str.assign(p);
    int n = p-szName;
    strPath.assign(szName, (n>0?n:strlen(szName)));
}
