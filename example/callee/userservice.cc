#include <iostream>
#include <string>
#include "user.pb.h"
#include "mprpcapplication.h"
#include "rpcprovider.h"
#include "logger.h"

class UserService : public fixbug::UserServiceRpc // rpc服务提供者
{
public:
    // 本地业务
    bool Login(std::string name, std::string pwd)
    {
        std::cout << "doing local service:Login" << std::endl;
        std::cout << "name:" << name << " pwd:" << pwd << std::endl;
        return true;
    }
    bool Register(uint32_t id, std::string name, std::string pwd)
    {
        std::cout << "doing local service:Register" << std::endl;
        std::cout << "id:" << id << " name:" << name << " pwd:" << pwd << std::endl;
        return true;
    }
    // 重写UserServiceRpc的虚函数
    // RPC接口函数，和RPC框架对接
    void Login(::google::protobuf::RpcController *controller,
               const ::fixbug::LoginRequest *request,
               ::fixbug::LoginResponse *response,
               ::google::protobuf::Closure *done)
    {
        // 远端请求Login，传到callee这里了，执行本地的Login业务
        // 获取数据
        std::string name = request->name();
        std::string pwd = request->pwd();
        // 本地业务
        bool login_result = Login(name, pwd);
        // 写入响应，即函数的返回
        fixbug::ResultCode *code = response->mutable_result();
        code->set_errcode(0);
        code->set_errmsg("");
        response->set_success(login_result);
        // 执行回调操作
        done->Run();
    }

    void Register(::google::protobuf::RpcController *controller,
                  const ::fixbug::RegisterRequest *request,
                  ::fixbug::RegisterResponse *response,
                  ::google::protobuf::Closure *done)
    {
        uint32_t id = request->id();
        std::string name = request->name();
        std::string pwd = request->pwd();
        bool register_result = Register(id, name, pwd);
        fixbug::ResultCode *code = response->mutable_result(); // 用mutable_result去修改子message
        code->set_errcode(0);
        code->set_errmsg("");
        response->set_success(register_result);
        done->Run();
    }
};
int main(int argc, char **argv)
{
    LOG_INFO("first log message!");
    LOG_INFO("%s:%s:%d", __FILE__, __FUNCTION__, __LINE__);

    // 框架初始化
    MprpcApplication::Init(argc, argv);
    // provider网络服务对象，把UserService对象发布到rpc节点
    RpcProvider provider;
    provider.NotifyService(new UserService());
    provider.Run();
}
