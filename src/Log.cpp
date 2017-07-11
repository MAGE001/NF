#include "Log.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>

using namespace log4cxx;
using namespace log4cxx::helpers;

CLog::CLog()
{
}

CLog::~CLog()
{
}

void CLog::getProcessName(std::string &str)
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
}

LoggerPtr CLog::Init(const char *cfg,const char *mod,ELOG_LEVEL lev)
{
    std::string strCfg;
    std::string strMod;
    if(cfg != NULL) {
        strCfg.assign(cfg);
    }
    if(mod != NULL) {
        strMod.assign(mod);
    }
    return Init(strCfg,strMod,lev);
}

LoggerPtr CLog::Init(std::string &cfg,std::string &mod,ELOG_LEVEL lev)
{
    LoggerPtr logger;
    std::string strLogName;
    try {
        if(!cfg.empty()) {
            log4cxx::PropertyConfigurator::configure(cfg);
        }

        if(!mod.empty()) {
            logger = log4cxx::Logger::getLogger(mod);
            strLogName = mod;
        } else {
            getProcessName(strLogName);
            logger = log4cxx::Logger::getLogger(strLogName);
        }

        if(cfg.empty()) {
            strLogName += ".log";
            AppenderPtr ptr = logger->getAppender(strLogName);
            if(ptr == NULL) {
                Pool p;
                log4cxx::PatternLayoutPtr layout = new log4cxx::PatternLayout("%-5p %d [%t/%m%n");

                log4cxx::RollingFileAppenderPtr appender(new log4cxx::RollingFileAppender(layout, log4cxx::LogString(strLogName), true));
                appender->setName(LOG4CXX_STR(strLogName));
                appender->setMaxFileSize(LOG4CXX_STR("100MB"));
                appender->setMaxBackupIndex(5);
                appender->activateOptions(p);
                logger->addAppender(appender);
                logger->setAdditivity(false);
            }
            setLevel(logger,lev);
        }
    } catch(Exception&e) {
    }
    return logger;
}

void CLog::setLevel(log4cxx::LoggerPtr &logger,ELOG_LEVEL lev)
{
    switch(lev) {
    case EDEBUG:
        logger->setLevel(log4cxx::Level::getDebug());
        break;
    case EWARN:
        logger->setLevel(log4cxx::Level::getWarn());
        break;
    case EERROR:
        logger->setLevel(log4cxx::Level::getError());
        break;
    case EFATAL:
        logger->setLevel(log4cxx::Level::getFatal());
        break;
    case EINFO:
    default:
        logger->setLevel(log4cxx::Level::getInfo());
        break;
    }
}

void CLog::setOption(log4cxx::LoggerPtr &logger,const char *option,const char *value)
{
    std::string strOpt,strVal;
    if(option)
        strOpt.assign(option);
    if(value)
        strVal.assign(value);
    setOption(logger,strOpt,strVal);
}

void CLog::setOption(log4cxx::LoggerPtr &logger,std::string &option,std::string &value)
{
    transform(option.begin(), option.end(), option.begin(),::toupper);
    if(option == "LEVEL") {
        transform(value.begin(), value.end(), value.begin(),::toupper);

        if(value == "CLOG::EDEBUG")
            setLevel(logger,CLog::EDEBUG);
        else if(value == "CLOG::EINFO")
            setLevel(logger,CLog::EINFO);
        else if(value == "CLOG::EWARN")
            setLevel(logger,CLog::EWARN);
        else if(value == "CLOG::EERROR")
            setLevel(logger,CLog::EERROR);
        else if(value == "CLOG::EFATAL")
            setLevel(logger,CLog::EFATAL);
    } else if(option == "FILESIZE") {
        setFileOption(logger,0,value);
    } else if(option == "FILENUM") {
        long num = strtol(value.c_str(),NULL,10);
        setFileOption(logger,1,value,num);
    } else if(option == "FILEOUT") {
        setFileOption(logger,2,value);
    } else if(option == "CFGFILE") {
        setFileOption(logger,3,value);
    } else {
        std::string name = logger->getName();
        name += ".log";

        AppenderPtr ptr = logger->getAppender(name);
        if(ptr == NULL) {
            return;
        }
        ptr->setOption(option, value);
        log4cxx::RollingFileAppenderPtr appender = (log4cxx::RollingFileAppenderPtr)ptr;
        Pool p;
        appender->activateOptions(p);
    }
}


int CLog::setFileOption(log4cxx::LoggerPtr &logger,int type,std::string &value,int num)
{
    std::string name = logger->getName();
    name += ".log";
    bool bActive = false;

    AppenderPtr ptr = logger->getAppender(name);
    if(ptr == NULL) {
        return -1;
    }

    log4cxx::RollingFileAppenderPtr appender = (log4cxx::RollingFileAppenderPtr)ptr;
    if(type == 0) {
        if(!value.empty()) {
            appender->setMaxFileSize(LOG4CXX_STR(value));
            bActive = true;
        }

    } else if(type == 1) {
        if(num > 0) {
            appender->setMaxBackupIndex(num);
            bActive = true;
        }
    } else if(type == 2) {
        if(!value.empty()) {
            appender->setFile(LOG4CXX_STR(value));
            bActive = true;
        }
    } else if(type == 3) {
        if(!value.empty()) {
            std::string name = logger->getName();

            logger->removeAllAppenders();
            logger->getLoggerRepository()->resetConfiguration();
            log4cxx::PropertyConfigurator::configure(value);
            logger = log4cxx::Logger::getLogger(name);
        }
    }

    if(bActive) {
        Pool p;
        appender->activateOptions(p);
    }
    return 0;
}

void CLog::LogDebug(LoggerPtr &logger,const char* pFile,const char *pFunc,int line,const char* pInfo,...)
{
    if (!LOG4CXX_UNLIKELY(logger->isDebugEnabled())){
        return;
    }

    char buf[1024] = {0};
    char message[4096] = {0};
    std::string strName = logger->getName();

    pid_t pid = getpid();

    snprintf(buf,sizeof(buf), "%u](%s:%d) %s %s %s", pid, pFile, line, strName.c_str(), pFunc,pInfo);

    va_list va;
    va_start(va, pInfo);
    vsnprintf(message, sizeof(message), buf, va);
    va_end(va);

    ::log4cxx::helpers::MessageBuffer oss_;
    logger->forcedLog(::log4cxx::Level::getDebug(), oss_.str(oss_ << message), LOG4CXX_LOCATION);
}


void CLog::LogInfo(LoggerPtr &logger,const char* pFile,const char *pFunc,int line,const char* pInfo,...)
{
    if (!logger->isInfoEnabled()) {
        return;
    }

    char buf[1024] = {0};
    char message[4096] = {0};
    std::string strName = logger->getName();

    pid_t pid = getpid();

    snprintf(buf, sizeof(buf), "%u](%s:%d) %s %s %s", pid, pFile, line, strName.c_str(), pFunc, pInfo);

    va_list va;
    va_start(va, pInfo);
    vsnprintf(message, sizeof(message), buf, va);
    va_end(va);

    ::log4cxx::helpers::MessageBuffer oss_;
    logger->forcedLog(::log4cxx::Level::getInfo(), oss_.str(oss_ << message), LOG4CXX_LOCATION);
}

void CLog::LogWarn(LoggerPtr &logger,const char* pFile,const char *pFunc,int line,const char* pInfo,...)
{
    if (!logger->isWarnEnabled()) {
        return;
    }

    char buf[1024] = {0};
    char message[4096] = {0};
    std::string strName = logger->getName();

    pid_t pid = getpid();

    snprintf(buf, sizeof(buf), "%u](%s:%d) %s %s %s", pid, pFile, line, strName.c_str(), pFunc, pInfo);

    va_list va;
    va_start(va, pInfo);
    vsnprintf(message, sizeof(message), buf, va);
    va_end(va);

    ::log4cxx::helpers::MessageBuffer oss_;

    logger->forcedLog(::log4cxx::Level::getWarn(), oss_.str(oss_ << message), LOG4CXX_LOCATION);
}

void CLog::LogError(LoggerPtr &logger,const char* pFile,const char *pFunc,int line,const char* pInfo,...)
{
    if (!logger->isErrorEnabled()) {
        return;
    }

    char buf[1024] = {0};
    char message[4096] = {0};
    std::string strName = logger->getName();

    pid_t pid = getpid();

    snprintf(buf, sizeof(buf), "%u](%s:%d) %s %s %s", pid, pFile, line, strName.c_str(), pFunc, pInfo);

    va_list va;
    va_start(va, pInfo);
    vsnprintf(message, sizeof(message), buf, va);
    va_end(va);

    ::log4cxx::helpers::MessageBuffer oss_;

    logger->forcedLog(::log4cxx::Level::getError(), oss_.str(oss_ << message), LOG4CXX_LOCATION);
}

void CLog::LogFatal(LoggerPtr &logger,const char* pFile,const char *pFunc,int line,const char* pInfo,...)
{
    if (!logger->isFatalEnabled()) {
        return;
    }

    char buf[1024] = {0};
    char message[4096] = {0};
    std::string strName = logger->getName();

    pid_t pid = getpid();

    snprintf(buf,sizeof(buf),"%u](%s:%d) %s %s %s",pid,pFile,line,strName.c_str(), pFunc,pInfo);

    va_list va;
    va_start(va, pInfo);
    vsnprintf(message, sizeof(message), buf, va);
    va_end(va);

    ::log4cxx::helpers::MessageBuffer oss_;

    logger->forcedLog(::log4cxx::Level::getFatal(), oss_.str(oss_ << message), LOG4CXX_LOCATION);
}
