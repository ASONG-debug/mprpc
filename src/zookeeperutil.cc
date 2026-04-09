#include "zookeeperutil.h"

#include "logger.h"
#include "mprpcapplication.h"
#include <algorithm>
#include <chrono>

void GlobalWatcher(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx)
{
    (void)zh;
    (void)path;

    ZkClient *client = static_cast<ZkClient *>(watcherCtx);
    if (client == nullptr)
    {
        return;
    }

    if (type == ZOO_SESSION_EVENT && state == ZOO_CONNECTED_STATE)
    {
        {
            std::lock_guard<std::mutex> lock(client->m_mutex);
            client->m_connected = true;
        }
        client->m_connectedCv.notify_all();
    }
}

ZkClient::ZkClient() : m_zhandle(nullptr), m_connected(false)
{
}

ZkClient::~ZkClient()
{
    if (m_zhandle != nullptr)
    {
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
    }
}

bool ZkClient::Start()
{
    if (m_zhandle != nullptr)
    {
        return true;
    }

    const std::string host = MprpcApplication::GetConfig().Load("zookeeperip");
    const std::string port = MprpcApplication::GetConfig().Load("zookeeperport");
    if (host.empty() || port.empty())
    {
        LOG_ERR("zookeeper config missing");
        return false;
    }

    const std::string connstr = host + ":" + port;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connected = false;
    }

    m_zhandle = zookeeper_init(connstr.c_str(), GlobalWatcher, 30000, nullptr, this, 0);
    if (m_zhandle == nullptr)
    {
        LOG_ERR("zookeeper_init failed, connstr=%s", connstr.c_str());
        return false;
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    const bool connected = m_connectedCv.wait_for(
        lock,
        std::chrono::seconds(5),
        [this]() { return m_connected; });

    if (!connected)
    {
        LOG_ERR("zookeeper connect timeout, connstr=%s", connstr.c_str());
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
        return false;
    }

    LOG_INFO("zookeeper connected, connstr=%s", connstr.c_str());
    return true;
}

bool ZkClient::Create(const std::string &path, const std::string &data, int state)
{
    if (m_zhandle == nullptr)
    {
        LOG_ERR("zookeeper create failed, client not connected, path=%s", path.c_str());
        return false;
    }

    const int exists_rc = zoo_exists(m_zhandle, path.c_str(), 0, nullptr);
    if (exists_rc == ZOK)
    {
        return true;
    }
    if (exists_rc != ZNONODE)
    {
        LOG_ERR("zookeeper exists failed, path=%s, rc=%d", path.c_str(), exists_rc);
        return false;
    }

    char path_buffer[512] = {0};
    int buffer_len = sizeof(path_buffer);
    const char *payload = data.empty() ? nullptr : data.c_str();
    const int payload_len = data.empty() ? 0 : static_cast<int>(data.size());
    const int create_rc = zoo_create(
        m_zhandle,
        path.c_str(),
        payload,
        payload_len,
        &ZOO_OPEN_ACL_UNSAFE,
        state,
        path_buffer,
        buffer_len);

    if (create_rc == ZOK || create_rc == ZNODEEXISTS)
    {
        LOG_INFO("zookeeper create path=%s state=%d", path.c_str(), state);
        return true;
    }

    LOG_ERR("zookeeper create failed, path=%s, rc=%d", path.c_str(), create_rc);
    return false;
}

std::string ZkClient::GetData(const std::string &path)
{
    if (m_zhandle == nullptr)
    {
        LOG_ERR("zookeeper get data failed, client not connected, path=%s", path.c_str());
        return "";
    }

    int buffer_len = 1024;
    std::string buffer(buffer_len, '\0');
    const int rc = zoo_get(m_zhandle, path.c_str(), 0, &buffer[0], &buffer_len, nullptr);
    if (rc != ZOK)
    {
        LOG_ERR("zookeeper get data failed, path=%s, rc=%d", path.c_str(), rc);
        return "";
    }

    buffer.resize(buffer_len);
    return buffer;
}

std::vector<std::string> ZkClient::GetChildren(const std::string &path)
{
    std::vector<std::string> children;
    if (m_zhandle == nullptr)
    {
        LOG_ERR("zookeeper get children failed, client not connected, path=%s", path.c_str());
        return children;
    }

    String_vector zk_children;
    const int rc = zoo_get_children(m_zhandle, path.c_str(), 0, &zk_children);
    if (rc != ZOK)
    {
        LOG_ERR("zookeeper get children failed, path=%s, rc=%d", path.c_str(), rc);
        return children;
    }

    children.reserve(zk_children.count);
    for (int i = 0; i < zk_children.count; ++i)
    {
        children.emplace_back(zk_children.data[i]);
    }
    deallocate_String_vector(&zk_children);
    std::sort(children.begin(), children.end());
    return children;
}
