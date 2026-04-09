#include "rpcprovider.h"

#include "logger.h"
#include "mprpcapplication.h"
#include "rpcprotocol.h"
#include "rpcheader.pb.h"
#include "zookeeperutil.h"
#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <google/protobuf/message.h>
#include <muduo/net/Buffer.h>

namespace
{
long long DurationMs(const std::chrono::steady_clock::time_point &start_time)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start_time)
        .count();
}
} // namespace

struct RpcProvider::RpcCallContext
{
    muduo::net::TcpConnectionPtr conn;
    uint64_t request_id = 0;
    std::string service_name;
    std::string method_name;
    std::chrono::steady_clock::time_point start_time;
    std::unique_ptr<google::protobuf::Message> request;
    std::unique_ptr<google::protobuf::Message> response;
};

RpcProvider::RpcProvider()
    : m_totalRequests(0), m_totalResponses(0), m_failedRequests(0)
{
}

RpcProvider::~RpcProvider()
{
}

void RpcProvider::NotifyService(google::protobuf::Service *service)
{
    if (service == nullptr)
    {
        LOG_ERR("notify service failed, service is null");
        return;
    }

    ServiceInfo service_info;
    service_info.m_service.reset(service);
    const google::protobuf::ServiceDescriptor *service_desc = service_info.m_service->GetDescriptor();
    if (service_desc == nullptr)
    {
        LOG_ERR("notify service failed, descriptor is null");
        return;
    }

    const std::string service_name = service_desc->name();
    for (int i = 0; i < service_desc->method_count(); ++i)
    {
        const google::protobuf::MethodDescriptor *method_desc = service_desc->method(i);
        service_info.m_methodMap.emplace(method_desc->name(), method_desc);
    }

    auto existing = m_serviceInfoMap.find(service_name);
    if (existing != m_serviceInfoMap.end())
    {
        LOG_ERR("duplicate service registration, service=%s", service_name.c_str());
        existing->second = std::move(service_info);
        return;
    }

    m_serviceInfoMap.emplace(service_name, std::move(service_info));
    LOG_INFO("service registered, service=%s method_count=%d", service_name.c_str(), service_desc->method_count());
}

void RpcProvider::Run()
{
    const std::string ip = MprpcApplication::GetConfig().Load("rpcserverip");
    const std::string port_text = MprpcApplication::GetConfig().Load("rpcserverport");
    const uint16_t port = static_cast<uint16_t>(std::atoi(port_text.c_str()));
    if (ip.empty() || port == 0)
    {
        LOG_ERR("rpc provider config invalid, ip=%s port=%s", ip.c_str(), port_text.c_str());
        return;
    }

    muduo::net::InetAddress address(ip, port);
    muduo::net::TcpServer server(&m_eventLoop, address, "RpcProvider");
    server.setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    server.setMessageCallback(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    server.setThreadNum(4);

    ZkClient zk_client;
    if (!zk_client.Start())
    {
        LOG_ERR("rpc provider failed to start zookeeper client");
        return;
    }

    const std::string endpoint = ip + ":" + std::to_string(port);
    for (auto &service_pair : m_serviceInfoMap)
    {
        const std::string service_path = "/" + service_pair.first;
        zk_client.Create(service_path);

        for (const auto &method_pair : service_pair.second.m_methodMap)
        {
            const std::string method_path = service_path + "/" + method_pair.first;
            zk_client.Create(method_path);
            const std::string endpoint_path = method_path + "/" + endpoint;
            zk_client.Create(endpoint_path, endpoint, ZOO_EPHEMERAL);
        }
    }

    LOG_INFO("rpc provider start ip=%s port=%u service_count=%llu",
             ip.c_str(),
             port,
             static_cast<unsigned long long>(m_serviceInfoMap.size()));
    server.start();
    m_eventLoop.loop();
}

void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn)
{
    if (!conn->connected())
    {
        LOG_INFO("rpc provider connection closed peer=%s", conn->peerAddress().toIpPort().c_str());
        conn->shutdown();
    }
}

void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn,
                            muduo::net::Buffer *buffer,
                            muduo::Timestamp)
{
    while (buffer->readableBytes() >= mprpc::kRpcFrameHeaderSize)
    {
        mprpc::RpcFrameHeader frame_header;
        if (!mprpc::ParseFrameHeader(buffer->peek(), mprpc::kRpcFrameHeaderSize, &frame_header))
        {
            LOG_ERR("rpc provider parse frame header failed peer=%s", conn->peerAddress().toIpPort().c_str());
            conn->shutdown();
            return;
        }

        std::string header_error;
        if (!mprpc::ValidateFrameHeader(frame_header, mprpc::kRpcMessageRequest, &header_error))
        {
            LOG_ERR("rpc provider invalid frame header request_id=%llu peer=%s error=%s",
                    static_cast<unsigned long long>(frame_header.request_id),
                    conn->peerAddress().toIpPort().c_str(),
                    header_error.c_str());
            sendErrorResponse(conn, frame_header.request_id, mprpc::kRpcStatusInvalidFrame, header_error);
            buffer->retrieveAll();
            return;
        }

        const std::size_t frame_size = mprpc::kRpcFrameHeaderSize + frame_header.metadata_size + frame_header.body_size;
        if (buffer->readableBytes() < frame_size)
        {
            return;
        }

        std::string frame = buffer->retrieveAsString(frame_size);
        std::string metadata = frame.substr(mprpc::kRpcFrameHeaderSize, frame_header.metadata_size);
        std::string body = frame.substr(mprpc::kRpcFrameHeaderSize + frame_header.metadata_size, frame_header.body_size);

        mprpc::RpcHeader request_meta;
        if (!request_meta.ParseFromString(metadata))
        {
            sendErrorResponse(conn, frame_header.request_id, mprpc::kRpcStatusInvalidMetadata, "parse request metadata failed");
            return;
        }

        const std::string service_name = request_meta.service_name();
        const std::string method_name = request_meta.method_name();
        if (request_meta.args_size() != body.size())
        {
            sendErrorResponse(conn,
                              frame_header.request_id,
                              mprpc::kRpcStatusInvalidMetadata,
                              "request body size mismatch",
                              service_name,
                              method_name);
            return;
        }

        auto service_it = m_serviceInfoMap.find(service_name);
        if (service_it == m_serviceInfoMap.end())
        {
            sendErrorResponse(conn,
                              frame_header.request_id,
                              mprpc::kRpcStatusServiceNotFound,
                              "service not found",
                              service_name,
                              method_name);
            return;
        }

        auto method_it = service_it->second.m_methodMap.find(method_name);
        if (method_it == service_it->second.m_methodMap.end())
        {
            sendErrorResponse(conn,
                              frame_header.request_id,
                              mprpc::kRpcStatusMethodNotFound,
                              "method not found",
                              service_name,
                              method_name);
            return;
        }

        google::protobuf::Service *service = service_it->second.m_service.get();
        const google::protobuf::MethodDescriptor *method = method_it->second;
        std::unique_ptr<google::protobuf::Message> request(service->GetRequestPrototype(method).New());
        std::unique_ptr<google::protobuf::Message> response(service->GetResponsePrototype(method).New());
        if (!request || !response)
        {
            sendErrorResponse(conn,
                              frame_header.request_id,
                              mprpc::kRpcStatusInternalError,
                              "create protobuf message failed",
                              service_name,
                              method_name);
            return;
        }

        if (!request->ParseFromString(body))
        {
            sendErrorResponse(conn,
                              frame_header.request_id,
                              mprpc::kRpcStatusRequestDecodeError,
                              "parse request body failed",
                              service_name,
                              method_name);
            return;
        }

        const uint64_t total_requests = ++m_totalRequests;
        LOG_INFO("rpc server recv request_id=%llu service=%s method=%s peer=%s total=%llu",
                 static_cast<unsigned long long>(frame_header.request_id),
                 service_name.c_str(),
                 method_name.c_str(),
                 conn->peerAddress().toIpPort().c_str(),
                 static_cast<unsigned long long>(total_requests));

        RpcCallContext *context = new RpcCallContext;
        context->conn = conn;
        context->request_id = frame_header.request_id;
        context->service_name = service_name;
        context->method_name = method_name;
        context->start_time = std::chrono::steady_clock::now();
        context->request = std::move(request);
        context->response = std::move(response);

        google::protobuf::Closure *done =
            google::protobuf::NewCallback(this, &RpcProvider::sendRpcResponse, context);
        try
        {
            service->CallMethod(method, nullptr, context->request.get(), context->response.get(), done);
        }
        catch (const std::exception &ex)
        {
            delete done;
            sendErrorResponse(conn,
                              context->request_id,
                              mprpc::kRpcStatusInternalError,
                              ex.what(),
                              context->service_name,
                              context->method_name);
            delete context;
            return;
        }
        catch (...)
        {
            delete done;
            sendErrorResponse(conn,
                              context->request_id,
                              mprpc::kRpcStatusInternalError,
                              "unknown service exception",
                              context->service_name,
                              context->method_name);
            delete context;
            return;
        }
    }
}

void RpcProvider::sendRpcResponse(RpcCallContext *context)
{
    if (context == nullptr)
    {
        return;
    }

    std::string response_body;
    if (!context->response->SerializeToString(&response_body))
    {
        sendErrorResponse(context->conn,
                          context->request_id,
                          mprpc::kRpcStatusResponseEncodeError,
                          "serialize response failed",
                          context->service_name,
                          context->method_name);
        delete context;
        return;
    }

    mprpc::RpcResponseMeta response_meta;
    response_meta.status_code = mprpc::kRpcStatusOk;
    const std::string response_meta_buffer = mprpc::SerializeResponseMeta(response_meta);

    mprpc::RpcFrameHeader response_header;
    response_header.message_type = mprpc::kRpcMessageResponse;
    response_header.request_id = context->request_id;
    response_header.metadata_size = static_cast<uint32_t>(response_meta_buffer.size());
    response_header.body_size = static_cast<uint32_t>(response_body.size());

    const std::string response_frame = mprpc::BuildFrame(response_header, response_meta_buffer, response_body);
    context->conn->send(response_frame);
    context->conn->shutdown();

    const uint64_t total_responses = ++m_totalResponses;
    const uint64_t failed_requests = m_failedRequests.load();
    LOG_INFO("rpc server done request_id=%llu service=%s method=%s cost_ms=%lld total=%llu success=%llu fail=%llu",
             static_cast<unsigned long long>(context->request_id),
             context->service_name.c_str(),
             context->method_name.c_str(),
             DurationMs(context->start_time),
             static_cast<unsigned long long>(total_responses),
             static_cast<unsigned long long>(total_responses - failed_requests),
             static_cast<unsigned long long>(failed_requests));

    delete context;
}

void RpcProvider::sendErrorResponse(const muduo::net::TcpConnectionPtr &conn,
                                    uint64_t request_id,
                                    uint32_t status_code,
                                    const std::string &error_text,
                                    const std::string &service_name,
                                    const std::string &method_name)
{
    mprpc::RpcResponseMeta response_meta;
    response_meta.status_code = status_code;
    response_meta.error_text = error_text;
    const std::string response_meta_buffer = mprpc::SerializeResponseMeta(response_meta);

    mprpc::RpcFrameHeader response_header;
    response_header.message_type = mprpc::kRpcMessageResponse;
    response_header.request_id = request_id;
    response_header.metadata_size = static_cast<uint32_t>(response_meta_buffer.size());
    response_header.body_size = 0;

    const std::string response_frame = mprpc::BuildFrame(response_header, response_meta_buffer, "");
    conn->send(response_frame);
    conn->shutdown();

    const uint64_t failed_requests = ++m_failedRequests;
    const uint64_t total_responses = ++m_totalResponses;
    LOG_ERR("rpc server fail request_id=%llu service=%s method=%s peer=%s status=%u error=%s total=%llu fail=%llu",
            static_cast<unsigned long long>(request_id),
            service_name.c_str(),
            method_name.c_str(),
            conn->peerAddress().toIpPort().c_str(),
            status_code,
            error_text.c_str(),
            static_cast<unsigned long long>(total_responses),
            static_cast<unsigned long long>(failed_requests));
}
