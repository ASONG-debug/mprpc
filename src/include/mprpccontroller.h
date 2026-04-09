#pragma once

#include <google/protobuf/service.h>
#include <cstdint>
#include <string>

class MprpcController : public google::protobuf::RpcController
{
public:
    MprpcController();
    void Reset();
    bool Failed() const;
    std::string ErrorText() const;
    void SetFailed(const std::string &reason);
    void StartCancel();
    bool IsCanceled() const;
    void NotifyOnCancel(google::protobuf::Closure *callback);
    void SetTimeoutMs(int timeout_ms);
    int TimeoutMs() const;
    void SetRequestId(uint64_t request_id);
    uint64_t RequestId() const;

private:
    bool m_failed;
    std::string m_errText;
    int m_timeoutMs;
    uint64_t m_requestId;
};
