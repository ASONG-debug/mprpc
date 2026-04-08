#include "mprpcchanel.h"
#include <string>
#include "rpcheader.pb.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include "zookeeperutil.h"
#include "mprpcapplication.h"
#include "mprpccontroller.h"
#include "logger.h"
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                              google::protobuf::RpcController *controller, const google::protobuf::Message *request,
                              google::protobuf::Message *response, google::protobuf::Closure *done)
{
    // 按照 header_size+service_name method_name args_size+args
    const google::protobuf::ServiceDescriptor *sd = method->service();
    std::string service_name = sd->name();    // service_name
    std::string method_name = method->name(); // method_name
    // 获取参数的序列化字符串长度
    std::string args_str;
    int args_size = 0;
    if (request->SerializeToString(&args_str))
    {
        args_size = args_str.size();
    }
    else
    {
        controller->SetFailed("serialIze request error!");
        LOG_ERR("serialIze request error!");
        return;
    }
    mprpc::RpcHeader rpcheader;
    rpcheader.set_service_name(service_name);
    rpcheader.set_method_name(method_name);
    rpcheader.set_args_size(args_size);
    // 序列化
    std::string rpc_header_str;
    uint32_t header_size = 0;
    if (rpcheader.SerializeToString(&rpc_header_str))
    {
        header_size = rpc_header_str.size();
    }
    else
    {
        controller->SetFailed("serialIze rpcheader error!");
        LOG_ERR("serialIze rpcheader error!");
        return;
    }
    // 组织待发送的rpc请求的字符串
    //[header_size(4字节)][rpc_header_str][args_str]
    std::string send_rpc_str;
    // 把 header_size 这个整数在内存中的前 4 个字节，直接拷贝出来，插到字符串开头。
    // 把这个长度值编码成 4 字节二进制数据，放到报文最前面。
    send_rpc_str.insert(0, std::string((char *)&header_size, 4)); // header_size
    send_rpc_str += rpc_header_str;                               // rpcheader
    send_rpc_str += args_str;                                     // args

    // TCP网络编程，发送即可-------------------------------------------------------
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1)
    {
        if (controller)
        {
            controller->SetFailed("create socket error!");
            LOG_ERR("create socket error!");
        }
        return;
    }
    // std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    // uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());

    // rpc调用方想要调用xx服务上的xx方法，需要查询zk上该服务所在的host信息

    ZkClient zkCli;
    zkCli.Start();
    std::string method_path = "/" + service_name + "/" + method_name;
    std::string host_data = zkCli.GetData(method_path.c_str());
    if (host_data == "")
    {
        controller->SetFailed(method_path + " is not exist!");
        LOG_ERR("%s create socket error!", method_path.c_str());
        return;
    }
    // ip:port
    int idx = host_data.find(":");
    if (idx == -1)
    {
        controller->SetFailed(method_path + " address is invalid");
        LOG_ERR("%s address is invalid!", method_path.c_str());
        return;
    }
    std::string ip = host_data.substr(0, idx);
    uint32_t port = atoi(host_data.substr(idx + 1).c_str());

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());
    // 连接rpc服务
    if (-1 == connect(clientfd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        close(clientfd);
        if (controller)
        {
            controller->SetFailed("connect socket error!");
            LOG_ERR("connect socket error!");
        }
        return;
    }
    // 发送rpc请求
    if (-1 == send(clientfd, send_rpc_str.c_str(), send_rpc_str.size(), 0))
    {
        close(clientfd);
        if (controller)
        {
            controller->SetFailed("send error!");
            LOG_ERR("send error!");
        }
        return;
    }
    // 接收rpc请求响应
    char recv_buf[1024] = {0};
    int recv_size = recv(clientfd, recv_buf, 1024, 0);
    if (-1 == recv_size)
    {
        close(clientfd);
        if (controller)
        {
            controller->SetFailed("recv error!");
            LOG_ERR("recv error!");
        }
        return;
    }
    // 反序列化rpc调用的响应数据
    if (!response->ParseFromArray(recv_buf, recv_size))
    {
        close(clientfd);
        if (controller)
        {
            controller->SetFailed("parse response error!");
            LOG_ERR("parse response error!");
        }
        return;
    }
    close(clientfd);
}