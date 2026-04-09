#include "mprpccontroller.h"

namespace
{
constexpr int kDefaultRpcTimeoutMs = 3000;
}

MprpcController::MprpcController()
{
    Reset();
}

void MprpcController::Reset()
{
    m_failed = false;
    m_errText.clear();
    m_timeoutMs = kDefaultRpcTimeoutMs;
    m_requestId = 0;
}

bool MprpcController::Failed() const
{
    return m_failed;
}

std::string MprpcController::ErrorText() const
{
    return m_errText;
}

void MprpcController::SetFailed(const std::string &reason)
{
    m_failed = true;
    m_errText = reason;
}

void MprpcController::StartCancel()
{
}

bool MprpcController::IsCanceled() const
{
    return false;
}

void MprpcController::NotifyOnCancel(google::protobuf::Closure *callback)
{
    (void)callback;
}

void MprpcController::SetTimeoutMs(int timeout_ms)
{
    if (timeout_ms > 0)
    {
        m_timeoutMs = timeout_ms;
    }
}

int MprpcController::TimeoutMs() const
{
    return m_timeoutMs;
}

void MprpcController::SetRequestId(uint64_t request_id)
{
    m_requestId = request_id;
}

uint64_t MprpcController::RequestId() const
{
    return m_requestId;
}
