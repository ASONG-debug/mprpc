#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include <zookeeper/zookeeper.h>

class ZkClient
{
public:
    ZkClient();
    ~ZkClient();

    bool Start();
    bool Create(const std::string &path, const std::string &data = "", int state = 0);
    std::string GetData(const std::string &path);
    std::vector<std::string> GetChildren(const std::string &path);

private:
    friend void GlobalWatcher(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx);

    zhandle_t *m_zhandle;
    std::mutex m_mutex;
    std::condition_variable m_connectedCv;
    bool m_connected;
};
