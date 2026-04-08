#include <iostream>
#include "mprpcapplication.h"
#include "user.pb.h"
int main(int argc, char **argv)
{
    // 整个程序启动以后，想使用mprpc框架来享受rpc服务调用，一定需要先调用框架的初始化函数（只初始化一次）
    MprpcApplication::Init(argc, argv);

    // Login方法
    fixbug::UserServiceRpc_Stub stub(new MprpcChannel());
    // rpc方法的请求
    fixbug::LoginRequest request;
    request.set_name("张三");
    request.set_pwd("123456");
    // rpc方法的响应
    fixbug::LoginResponse response;
    MprpcController controller;
    stub.Login(&controller, &request, &response, nullptr); // 通过RpcChannel->RpcChannel::callMethod来做

    // 一次rpc调用完成，读调用响应
    if (controller.Failed())
    {
        std::cout << controller.ErrorText() << std::endl;
    }
    else
    {
        if (response.result().errcode() == 0)
        {
            std::cout << "rpc login response:" << response.success() << std::endl;
        }
        else
        {
            std::cout << "rpc login response error : " << response.result().errmsg() << std::endl;
        }
    }

    // Register方法
    fixbug::RegisterRequest req;
    req.set_id(2000);
    req.set_name("李四");
    req.set_pwd("1234568");
    fixbug::RegisterResponse res;
    stub.Register(nullptr, &req, &res, nullptr);
    if (controller.Failed())
    {
        std::cout << controller.ErrorText() << std::endl;
    }
    else
    {
        if (response.result().errcode() == 0)
        {
            std::cout << "rpc Register response:" << response.success() << std::endl;
        }
        else
        {
            std::cout << "rpc Register response error : " << response.result().errmsg() << std::endl;
        }
    }
    return 0;
}