#include <iostream>
#include "mprpcapplication.h"
#include "mprpcchanel.h"
#include "mprpccontroller.h"
#include "user.pb.h"

int main(int argc, char **argv)
{
    MprpcApplication::Init(argc, argv);

    fixbug::UserServiceRpc_Stub stub(new MprpcChannel());

    fixbug::LoginRequest login_request;
    login_request.set_name("zhang san");
    login_request.set_pwd("123456");
    fixbug::LoginResponse login_response;
    MprpcController login_controller;
    login_controller.SetTimeoutMs(3000);
    stub.Login(&login_controller, &login_request, &login_response, nullptr);

    if (login_controller.Failed())
    {
        std::cout << "login failed, request_id=" << login_controller.RequestId()
                  << ", error=" << login_controller.ErrorText() << std::endl;
    }
    else if (login_response.result().errcode() == 0)
    {
        std::cout << "login ok, request_id=" << login_controller.RequestId()
                  << ", success=" << login_response.success() << std::endl;
    }
    else
    {
        std::cout << "login response error, request_id=" << login_controller.RequestId()
                  << ", errmsg=" << login_response.result().errmsg() << std::endl;
    }

    fixbug::RegisterRequest register_request;
    register_request.set_id(2000);
    register_request.set_name("li si");
    register_request.set_pwd("1234568");
    fixbug::RegisterResponse register_response;
    MprpcController register_controller;
    register_controller.SetTimeoutMs(3000);
    stub.Register(&register_controller, &register_request, &register_response, nullptr);

    if (register_controller.Failed())
    {
        std::cout << "register failed, request_id=" << register_controller.RequestId()
                  << ", error=" << register_controller.ErrorText() << std::endl;
    }
    else if (register_response.result().errcode() == 0)
    {
        std::cout << "register ok, request_id=" << register_controller.RequestId()
                  << ", success=" << register_response.success() << std::endl;
    }
    else
    {
        std::cout << "register response error, request_id=" << register_controller.RequestId()
                  << ", errmsg=" << register_response.result().errmsg() << std::endl;
    }

    return 0;
}
