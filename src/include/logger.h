#pragma once
#include "lockqueue.h"
// rpc框架日志系统
enum LogLevel
{
    INFO,  // 普通信息
    ERROR, // 错误信息
};
struct LogTask
{
    LogLevel level;
    std::string msg;
};

class Logger
{
public:
    // 获取单例
    static Logger &GetInstance();
    // 设置日志级别
    void SetLogLevel(LogLevel level);
    // 写日志
    void Log(const std::string &msg);

private:
    int m_loglevel;                  // 记录日志级别
    LockQueue<std::string> m_lckQue; // 日志缓冲队列

    Logger();
    Logger(const Logger &) = delete;
    Logger(Logger &&) = delete;
};
// 定义宏LOG_INFO("xXX%d%s"，20,"xXXx")
#define LOG_INFO(logmsgformat, ...)                               \
    do                                                            \
    {                                                             \
        Logger &logger = Logger::GetInstance();                   \
        logger.SetLogLevel(INFO);                                 \
        char c[1024] = {0};                                       \
        snprintf(c, 1024, "[info] " logmsgformat, ##__VA_ARGS__); \
        logger.Log(c);                                            \
    } while (0);

// 定义宏LOG_ERR("xXX%d%s"，20,"xXXx")
#define LOG_ERR(logmsgformat, ...)                                 \
    do                                                             \
    {                                                              \
        Logger &logger = Logger::GetInstance();                    \
        logger.SetLogLevel(ERROR);                                 \
        char c[1024] = {0};                                        \
        snprintf(c, 1024, "[error] " logmsgformat, ##__VA_ARGS__); \
        logger.Log(c);                                             \
    } while (0);
