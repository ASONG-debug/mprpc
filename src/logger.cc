#include "logger.h"
#include "time.h"
#include <iostream>
Logger &Logger::GetInstance()
{
    static Logger logger;
    return logger;
}
Logger::Logger()
{
    // 启动专门的写日志线程 队列-->磁盘
    std::thread writeLogTask([&]()
                             {
    // 获取时期，获取日志信息，写入日志文件
        for (;;)
        {
            time_t now = time(nullptr);
            tm *nowtm = localtime(&now);

            char file_name[128];
            sprintf(file_name, "%d-%d-%d-log.txt", nowtm->tm_year + 1900, nowtm->tm_mon + 1, nowtm->tm_mday);

            // a+ 文件不存在就创建文件，存在就写在末尾
            FILE *pf = fopen(file_name, "a+");
            if (pf == nullptr)
            {
                std::cout << "logger file :" << file_name << " open error!" << std::endl;
                exit(EXIT_FAILURE);
            }
            std::string msg=m_lckQue.pop();

            //日志内容 前面添加时分秒
            char time_buf[128] = {0};
            sprintf(time_buf, "%d:%d:%d =>",
                    nowtm->tm_hour, 
                    nowtm->tm_min, 
                    nowtm->tm_sec);
            msg.insert(0, time_buf);
            msg.append("\n");
            //写入内容
            fputs(msg.c_str(),pf);
            fclose(pf);
        } });
    // detach()在后台跑，不管主线程
    writeLogTask.detach();
}

void Logger::SetLogLevel(LogLevel level)
{
    m_loglevel = level;
}
// 写日志->写入缓冲队列
void Logger::Log(const std::string &msg)
{
    m_lckQue.push(msg);
}