#include "rpcprovider.h"
#include <string>
#include "mprpcapplication.h"
#include "zookeeperutil.h"
#include <functional>
#include <google/protobuf/descriptor.h>
#include "rpcheader.pb.h"
#include"logger.h"
// 框架提供给外部使用，发布rpc方法的函数接口
void RpcProvider::NotifyService(google::protobuf::Service *Service)
{
    ServiceInfo serviceInfo;
    // 获取服务对象的描述信息
    const google::protobuf::ServiceDescriptor *pserviceDesc = Service->GetDescriptor();
    // 获取服务名字
    std::string service_name = pserviceDesc->name();
    // 获取服务对象Service的方法数量
    int methodCnt = pserviceDesc->method_count();
    for (int i = 0; i < methodCnt; i++)
    {
        // 获取了服务对象指定下标的服务方法的描述（抽象描述）
        const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
        std::string method_name = pmethodDesc->name();
        serviceInfo.m_methodMap.insert({method_name, pmethodDesc});
    }
    serviceInfo.m_service = Service;
    m_serviceInfoMap.insert({service_name, serviceInfo});
}
// 启动rpc服务
void RpcProvider::Run()
{
    std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());
    
    muduo::net::InetAddress address(ip, port);
    // 创建TcpServer对象
    muduo::net::TcpServer server(&m_eventLoop, address, "RpcProvider");
    // 绑定连接回调和消息读写回调方法
    server.setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    server.setMessageCallback(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    // 设置muduo库的线程数量
    server.setThreadNum(4);

    //把当前rpc节点上要发布的服务注册到zk上面，让rpc client可以从zk上发现服务
    ZkClient zkCli;
    zkCli.Start();//连接zk服务器
    //service_name为永久性节点 method_name为临时性节点
    for(auto& sp:m_serviceInfoMap)
    {
        //service_name
        std::string service_path="/"+sp.first;
        zkCli.Create(service_path.c_str(),nullptr,0);
        for(auto &mp:sp.second.m_methodMap)
        {
            //  路径 /service_name/method_name
            std::string method_path=service_path+"/"+mp.first;
            char method_path_data[128]={0};
            sprintf(method_path_data,"%s:%d",ip.c_str(),port);
            zkCli.Create(method_path.c_str(),method_path_data,strlen(method_path_data),ZOO_EPHEMERAL);
        }
    }
    std::cout << "RpcProvider start service at ip:" << ip << "port:" << port << std::endl;
    // 启动网络服务
    server.start();
    m_eventLoop.loop();
}

void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn)
{
    if (!conn->connected())
    {
        conn->shutdown();
    }
}
// 远端发起rpc调用请求，muduo会响应onmessage
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn,
                            muduo::net::Buffer *buffer,
                            muduo::Timestamp)
{
    std::string rec_buf = buffer->retrieveAllAsString();

    // 从字符流中读取前四个字节的内容
    uint32_t header_size = 0;
    rec_buf.copy((char *)&header_size, 4, 0);
    // 根据header_size读取数据头的原始字符流，反序列数据，得到rpc请求的详细信息
    std::string rpc_header_str = rec_buf.substr(4, header_size);

    mprpc::RpcHeader rpcHeader;
    std::string service_name;
    std::string method_name;
    uint32_t args_size;
    if (rpcHeader.ParseFromString(rpc_header_str))
    {
        // 反序列化成功
        service_name = rpcHeader.service_name();
        method_name = rpcHeader.method_name();
        args_size = rpcHeader.args_size();
    }
    else
    {
        // 数据头反序列化失败
        std::cout << "rpc_header_str:" << rpc_header_str << " parse error!" << std::endl;
        return;
    }
    // 获取rpc方法参数
    std::string args_str = rec_buf.substr(4 + header_size, args_size);
    // 获取service对象和method对象
    auto it = m_serviceInfoMap.find(service_name);
    if (it == m_serviceInfoMap.end())
    {
        std::cout << service_name << " is not exist!" << std::endl;
        return;
    }
    auto mit = it->second.m_methodMap.find(method_name);
    if (mit == it->second.m_methodMap.end())
    {
        std::cout << service_name << ":" << method_name << " is not exist!" << std::endl;
        return;
    }
    google::protobuf::Service *service = it->second.m_service;
    const google::protobuf::MethodDescriptor *method = mit->second;
    // 把参数反序列化
    google::protobuf::Message *request = service->GetRequestPrototype(method).New();
    if (!request->ParseFromString(args_str))
    {
        std::cout << "request parse error, content:" << args_str << std::endl;
        return;
    }
    google::protobuf::Message *response = service->GetResponsePrototype(method).New();

    // 给method方法调用，绑定一个closure的回调函数
    google::protobuf::Closure*done=google::protobuf::NewCallback<RpcProvider,const muduo::net::TcpConnectionPtr&,google::protobuf::Message*>(this,&RpcProvider::sendRpcResponse,conn,response);
    //调用业务层callee的对应方法
    service->CallMethod(method, nullptr, request, response, done);
}
//closure的回调操作，这里用于序列化rpc响应结果，并发送对端
void RpcProvider::sendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response)
{
    std::string response_str;
    if(response->SerializeToString(&response_str))
    {
        //序列化成功后，通过网络把rpc方法执行的结果发送回rpc的调用方
        conn->send(response_str);
    }
    else
    {
        std::cout << "serialize response_str error!" << std::endl;
    }
    conn->shutdown();//模拟短链接，主动断开连接
}

RpcProvider::RpcProvider()
{
}
RpcProvider::~RpcProvider()
{
}