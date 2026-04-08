#pragma once
#include "google/protobuf/service.h"
#include<google/protobuf/descriptor.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include<functional>
#include<unordered_map>

class RpcProvider
{
private:
    muduo::net::EventLoop m_eventLoop;
    void OnConnection(const muduo::net::TcpConnectionPtr& conn);
    void OnMessage(const muduo::net::TcpConnectionPtr &conn,muduo::net::Buffer *buffer, muduo::Timestamp);
    struct ServiceInfo
    {
        //方法名->方法描述
        google::protobuf::Service* m_service;
        std::unordered_map<std::string,const google::protobuf::MethodDescriptor*>m_methodMap;
    };
    std::unordered_map<std::string,ServiceInfo>m_serviceInfoMap;
    //closure的回调操作，用于序列化rpc的响应和网络发送
    void sendRpcResponse(const muduo::net::TcpConnectionPtr& conn,google::protobuf::Message *response);
public:
    // 接收任意服务
    void NotifyService(google::protobuf::Service *Service);
    // 启动rpc服务
    void Run();
    RpcProvider();
    ~RpcProvider();
};