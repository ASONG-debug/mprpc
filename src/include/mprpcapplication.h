#pragma once
#include"mprpcconfig.h"
#include "mprpcchanel.h"
#include"mprpccontroller.h"
//mprpc框架的基础类，负责框架初始化
class MprpcApplication
{
public:
    static void Init(int argc,char **argv);
    //单例模式
    static MprpcApplication& GetInstance();
    static MprpcConfig& GetConfig();
private:
    static MprpcConfig m_config;
    MprpcApplication();
    //禁止拷贝构造
    MprpcApplication(const MprpcApplication&)=delete;
    //禁止移动构造
    MprpcApplication(const MprpcApplication&&)=delete;
};
