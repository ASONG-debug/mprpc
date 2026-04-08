 #include"test.pb.h"
 #include<iostream>
 using namespace fixbug;
 int main()
 {
    LoginRequest reqA;
    reqA.set_name("zhang san");
    reqA.set_pwd("123");
    std::string send_str;
    //序列化
    if(reqA.SerializeToString(&send_str))
    {
        std::cout<<send_str<<std::endl;
        std::cout<<send_str.size()<<std::endl;
    }
    //反序列化
    LoginRequest reqB;
    if(reqB.ParseFromString(send_str))
    {
        std::cout<<reqB.name()<<std::endl;
        std::cout<<reqB.pwd()<<std::endl;
    }

    return 0;
 }