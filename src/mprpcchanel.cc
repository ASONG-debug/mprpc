#include "mprpcchanel.h"

#include "logger.h"
#include "mprpccontroller.h"
#include "rpcprotocol.h"
#include "rpcheader.pb.h"
#include "zookeeperutil.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace
{
constexpr int kDefaultRpcTimeoutMs = 3000;
std::mutex g_roundRobinMutex;
std::unordered_map<std::string, std::size_t> g_roundRobinCursor;
uint64_t g_requestId = 0;

uint64_t NextRequestId()
{
    static std::mutex request_id_mutex;
    std::lock_guard<std::mutex> lock(request_id_mutex);
    return ++g_requestId;
}

void SetControllerFailed(google::protobuf::RpcController *controller, const std::string &error_text)
{
    if (controller != nullptr)
    {
        controller->SetFailed(error_text);
    }
}

MprpcController *GetController(google::protobuf::RpcController *controller)
{
    return dynamic_cast<MprpcController *>(controller);
}

int GetTimeoutMs(google::protobuf::RpcController *controller)
{
    MprpcController *mprpc_controller = GetController(controller);
    if (mprpc_controller == nullptr)
    {
        return kDefaultRpcTimeoutMs;
    }
    return mprpc_controller->TimeoutMs();
}

void SetRequestId(google::protobuf::RpcController *controller, uint64_t request_id)
{
    MprpcController *mprpc_controller = GetController(controller);
    if (mprpc_controller != nullptr)
    {
        mprpc_controller->SetRequestId(request_id);
    }
}

bool ConfigureSocketTimeouts(int fd, int timeout_ms)
{
    if (timeout_ms <= 0)
    {
        return true;
    }

    timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
}

bool ConnectWithTimeout(int fd, const sockaddr_in &server_addr, int timeout_ms)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        return false;
    }

    const int rc = connect(fd, reinterpret_cast<const sockaddr *>(&server_addr), sizeof(server_addr));
    if (rc == 0)
    {
        fcntl(fd, F_SETFL, flags);
        return true;
    }

    if (errno != EINPROGRESS)
    {
        fcntl(fd, F_SETFL, flags);
        return false;
    }

    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(fd, &write_fds);

    timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    const int select_rc = select(fd + 1, nullptr, &write_fds, nullptr, &timeout);
    if (select_rc <= 0)
    {
        errno = (select_rc == 0) ? ETIMEDOUT : errno;
        fcntl(fd, F_SETFL, flags);
        return false;
    }

    int socket_error = 0;
    socklen_t error_len = sizeof(socket_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_len) == -1)
    {
        fcntl(fd, F_SETFL, flags);
        return false;
    }

    fcntl(fd, F_SETFL, flags);
    if (socket_error != 0)
    {
        errno = socket_error;
        return false;
    }

    return true;
}

bool SendAll(int fd, const std::string &buffer)
{
    std::size_t total_sent = 0;
    while (total_sent < buffer.size())
    {
        const ssize_t sent = send(fd, buffer.data() + total_sent, buffer.size() - total_sent, 0);
        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (sent == 0)
        {
            return false;
        }
        total_sent += static_cast<std::size_t>(sent);
    }
    return true;
}

bool RecvAll(int fd, std::string *buffer, std::size_t bytes_to_read)
{
    if (buffer == nullptr)
    {
        return false;
    }

    buffer->assign(bytes_to_read, '\0');
    std::size_t total_read = 0;
    while (total_read < bytes_to_read)
    {
        const ssize_t received = recv(fd, &(*buffer)[total_read], bytes_to_read - total_read, 0);
        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (received == 0)
        {
            return false;
        }
        total_read += static_cast<std::size_t>(received);
    }
    return true;
}

bool ParseEndpoint(const std::string &endpoint, std::string *ip, uint16_t *port)
{
    const std::size_t separator = endpoint.rfind(':');
    if (separator == std::string::npos || separator == endpoint.size() - 1)
    {
        return false;
    }

    const std::string ip_str = endpoint.substr(0, separator);
    const std::string port_str = endpoint.substr(separator + 1);
    const int parsed_port = std::atoi(port_str.c_str());
    if (parsed_port <= 0 || parsed_port > 65535)
    {
        return false;
    }

    if (ip != nullptr)
    {
        *ip = ip_str;
    }
    if (port != nullptr)
    {
        *port = static_cast<uint16_t>(parsed_port);
    }
    return true;
}

std::string PickEndpointRoundRobin(const std::string &path, const std::vector<std::string> &endpoints)
{
    std::lock_guard<std::mutex> lock(g_roundRobinMutex);
    std::size_t &cursor = g_roundRobinCursor[path];
    const std::string endpoint = endpoints[cursor % endpoints.size()];
    cursor = (cursor + 1) % endpoints.size();
    return endpoint;
}

long long DurationMs(const std::chrono::steady_clock::time_point &start_time)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start_time)
        .count();
}
} // namespace

void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                              google::protobuf::RpcController *controller,
                              const google::protobuf::Message *request,
                              google::protobuf::Message *response,
                              google::protobuf::Closure *done)
{
    const auto start_time = std::chrono::steady_clock::now();
    const google::protobuf::ServiceDescriptor *service_descriptor = method->service();
    const std::string service_name = service_descriptor->name();
    const std::string method_name = method->name();
    const uint64_t request_id = NextRequestId();
    SetRequestId(controller, request_id);

    std::string request_body;
    if (!request->SerializeToString(&request_body))
    {
        SetControllerFailed(controller, "serialize request failed");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s serialize request failed",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str());
        return;
    }

    mprpc::RpcHeader request_meta;
    request_meta.set_service_name(service_name);
    request_meta.set_method_name(method_name);
    request_meta.set_args_size(static_cast<uint32_t>(request_body.size()));

    std::string request_meta_buffer;
    if (!request_meta.SerializeToString(&request_meta_buffer))
    {
        SetControllerFailed(controller, "serialize request metadata failed");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s serialize metadata failed",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str());
        return;
    }

    ZkClient zk_client;
    if (!zk_client.Start())
    {
        SetControllerFailed(controller, "zookeeper connection failed");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s zookeeper start failed",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str());
        return;
    }

    const std::string method_path = "/" + service_name + "/" + method_name;
    const std::vector<std::string> endpoints = zk_client.GetChildren(method_path);
    if (endpoints.empty())
    {
        SetControllerFailed(controller, method_path + " has no available provider");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s no provider",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str());
        return;
    }

    const std::string endpoint = PickEndpointRoundRobin(method_path, endpoints);
    std::string ip;
    uint16_t port = 0;
    if (!ParseEndpoint(endpoint, &ip, &port))
    {
        SetControllerFailed(controller, "invalid provider endpoint: " + endpoint);
        LOG_ERR("rpc client request_id=%llu service=%s method=%s invalid endpoint=%s",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str(),
                endpoint.c_str());
        return;
    }

    mprpc::RpcFrameHeader request_header;
    request_header.message_type = mprpc::kRpcMessageRequest;
    request_header.request_id = request_id;
    request_header.metadata_size = static_cast<uint32_t>(request_meta_buffer.size());
    request_header.body_size = static_cast<uint32_t>(request_body.size());
    const std::string request_frame = mprpc::BuildFrame(request_header, request_meta_buffer, request_body);

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1)
    {
        SetControllerFailed(controller, "create socket failed");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s create socket failed: %s",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str(),
                std::strerror(errno));
        return;
    }

    auto close_fd = [&client_fd]() {
        if (client_fd != -1)
        {
            close(client_fd);
            client_fd = -1;
        }
    };

    const int timeout_ms = GetTimeoutMs(controller);
    if (!ConfigureSocketTimeouts(client_fd, timeout_ms))
    {
        SetControllerFailed(controller, "configure socket timeout failed");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s configure timeout failed: %s",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str(),
                std::strerror(errno));
        close_fd();
        return;
    }

    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) != 1)
    {
        SetControllerFailed(controller, "invalid provider ip: " + ip);
        LOG_ERR("rpc client request_id=%llu service=%s method=%s invalid ip=%s",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str(),
                ip.c_str());
        close_fd();
        return;
    }

    if (!ConnectWithTimeout(client_fd, server_addr, timeout_ms))
    {
        const std::string error_text = errno == ETIMEDOUT ? "connect timeout" : std::strerror(errno);
        SetControllerFailed(controller, error_text);
        LOG_ERR("rpc client request_id=%llu service=%s method=%s connect endpoint=%s failed: %s",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str(),
                endpoint.c_str(),
                error_text.c_str());
        close_fd();
        return;
    }

    if (!SendAll(client_fd, request_frame))
    {
        SetControllerFailed(controller, "send request failed");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s send endpoint=%s failed: %s",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str(),
                endpoint.c_str(),
                std::strerror(errno));
        close_fd();
        return;
    }

    std::string response_header_buffer;
    if (!RecvAll(client_fd, &response_header_buffer, mprpc::kRpcFrameHeaderSize))
    {
        const std::string error_text = errno == EAGAIN || errno == EWOULDBLOCK ? "recv response timeout" : "recv response header failed";
        SetControllerFailed(controller, error_text);
        LOG_ERR("rpc client request_id=%llu service=%s method=%s recv header endpoint=%s failed: %s",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str(),
                endpoint.c_str(),
                std::strerror(errno));
        close_fd();
        return;
    }

    mprpc::RpcFrameHeader response_header;
    if (!mprpc::ParseFrameHeader(response_header_buffer.data(), response_header_buffer.size(), &response_header))
    {
        SetControllerFailed(controller, "parse response header failed");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s parse response header failed",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str());
        close_fd();
        return;
    }

    std::string header_error;
    if (!mprpc::ValidateFrameHeader(response_header, mprpc::kRpcMessageResponse, &header_error))
    {
        SetControllerFailed(controller, header_error);
        LOG_ERR("rpc client request_id=%llu service=%s method=%s invalid response header: %s",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str(),
                header_error.c_str());
        close_fd();
        return;
    }

    if (response_header.request_id != request_id)
    {
        SetControllerFailed(controller, "response request_id mismatch");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s response request_id mismatch=%llu",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str(),
                static_cast<unsigned long long>(response_header.request_id));
        close_fd();
        return;
    }

    std::string response_meta_buffer;
    if (!RecvAll(client_fd, &response_meta_buffer, response_header.metadata_size))
    {
        SetControllerFailed(controller, "recv response metadata failed");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s recv metadata failed",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str());
        close_fd();
        return;
    }

    std::string response_body_buffer;
    if (!RecvAll(client_fd, &response_body_buffer, response_header.body_size))
    {
        SetControllerFailed(controller, "recv response body failed");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s recv body failed",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str());
        close_fd();
        return;
    }

    mprpc::RpcResponseMeta response_meta;
    if (!mprpc::ParseResponseMeta(response_meta_buffer, &response_meta))
    {
        SetControllerFailed(controller, "parse response metadata failed");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s parse response metadata failed",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str());
        close_fd();
        return;
    }

    if (response_meta.status_code != mprpc::kRpcStatusOk)
    {
        SetControllerFailed(controller, response_meta.error_text.empty() ? "rpc server returned error" : response_meta.error_text);
        LOG_ERR("rpc client request_id=%llu service=%s method=%s endpoint=%s server_error=%s",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str(),
                endpoint.c_str(),
                response_meta.error_text.c_str());
        close_fd();
        return;
    }

    if (response == nullptr || !response->ParseFromString(response_body_buffer))
    {
        SetControllerFailed(controller, "parse response body failed");
        LOG_ERR("rpc client request_id=%llu service=%s method=%s parse response body failed",
                static_cast<unsigned long long>(request_id),
                service_name.c_str(),
                method_name.c_str());
        close_fd();
        return;
    }

    close_fd();
    LOG_INFO("rpc client request_id=%llu service=%s method=%s endpoint=%s cost_ms=%lld status=ok",
             static_cast<unsigned long long>(request_id),
             service_name.c_str(),
             method_name.c_str(),
             endpoint.c_str(),
             DurationMs(start_time));

    if (done != nullptr)
    {
        done->Run();
    }
}
