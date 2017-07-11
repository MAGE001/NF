#ifndef __NF_MESSAGE_H__
#define __NF_MESSAGE_H__

#include "NFGlobal.h"
#include "NFTypedef.h"

struct NF_EPDATA_HEAD
{
    volatile int fd;
};

struct NF_MSG_HEAD
{
    UINT32 ulMsgSize;  //include Head
    UINT32 ulMsgType;
    UINT16 usSend;
    UINT16 usRecv;
};

struct NF_MSG_ACK_HEAD: public NF_MSG_HEAD
{
    UINT32 ulReturnCode;
};

#endif

