#pragma once
#include <zookeeper/zookeeper.h>
#include <string>

class ZkClient
{
public:
    ZkClient();
    ~ZkClient();

    // 连接 zookeeper server
    void Start();

    // 在 zookeeper 上创建节点
    // path: 节点路径  data: 节点数据  state: 节点类型
    void Create(const char *path, const char *data, int datalen, int state = 0);

    // 获取节点的值
    std::string GetData(const char *path);

private:
    zhandle_t *m_zhandle; // zookeeper 客户端句柄
};