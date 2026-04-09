#pragma once

#include "google/protobuf/service.h"
#include <google/protobuf/descriptor.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/TcpServer.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

class RpcProvider
{
public:
    RpcProvider();
    ~RpcProvider();

    void NotifyService(google::protobuf::Service *service);
    void Run();

private:
    struct RpcCallContext;

    struct ServiceInfo
    {
        std::unique_ptr<google::protobuf::Service> m_service;
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor *> m_methodMap;
    };

    void OnConnection(const muduo::net::TcpConnectionPtr &conn);
    void OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buffer, muduo::Timestamp);
    void sendRpcResponse(RpcCallContext *context);
    void sendErrorResponse(const muduo::net::TcpConnectionPtr &conn,
                           uint64_t request_id,
                           uint32_t status_code,
                           const std::string &error_text,
                           const std::string &service_name = "",
                           const std::string &method_name = "");

    muduo::net::EventLoop m_eventLoop;
    std::unordered_map<std::string, ServiceInfo> m_serviceInfoMap;
    std::atomic<uint64_t> m_totalRequests;
    std::atomic<uint64_t> m_totalResponses;
    std::atomic<uint64_t> m_failedRequests;
};
