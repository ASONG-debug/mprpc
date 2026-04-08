#include "mprpcapplication.h"
#include <iostream>
#include <unistd.h>
MprpcConfig MprpcApplication::m_config;
void ShowArgsHelp()
{
    std::cout << "format: command -i <configfile>" << std::endl;
}
MprpcApplication &MprpcApplication::GetInstance()
{
    static MprpcApplication app;
    return app;
}
void MprpcApplication::Init(int argc, char **argv)
{
    if (argc < 2)
    {
        ShowArgsHelp();
        exit(EXIT_FAILURE);
    }
    int opt;
    std::string config_file;
    while ((opt = getopt(argc, argv, "i:")) != -1)
    {
        switch (opt)
        {
        case 'i':
            config_file = optarg;//存放文件路径
            break;
        case '?':
            exit(EXIT_FAILURE);
        case ':':
            exit(EXIT_FAILURE);
        default:
            break;
        }
    }
    // 加载配置文件rpcserver_ip= rpcserver_port= zookeeper_ip= zookepper_port=
    m_config.LoadConfigFile(config_file.c_str());
    // std::cout << "rpcserverip:" << m_config.Load("rpcserverip") << std::endl;
    // std::cout << "rpcserverport:" << m_config.Load("rpcserverport") << std::endl;
    // std::cout << "zookeeperip:" << m_config.Load("zookeeperip") << std::endl;
    // std::cout << "zookeeperport:" << m_config.Load("zookeeperport") << std::endl;
}

MprpcConfig& MprpcApplication::GetConfig()
{
    return m_config;
}

MprpcApplication::MprpcApplication()
{
}