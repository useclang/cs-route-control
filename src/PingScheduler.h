#pragma once

#include "ServerRegion.h"

#include <QObject>
#include <QThreadPool>

#include <atomic>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ipexport.h>
#include <iphlpapi.h>
#include <icmpapi.h>

class PingScheduler : public QObject {
    Q_OBJECT

public:
    struct Result {
        std::string code;
        int         avg    = -1;
        double      jitter = 0.0;
    };

    explicit PingScheduler(QObject *parent = nullptr);
    ~PingScheduler() override;

    void schedule(const std::unordered_map<std::string, ServerRegion> &regions);
    void cancel();

signals:
    void resultReady(PingScheduler::Result result);
    void cycleFinished();

private:
    static int    pingOnce(HANDLE hIcmp, IN_ADDR addr, DWORD timeoutMs);
    static Result pingRegion(HANDLE hIcmp, const std::string &code,
                             std::shared_ptr<const std::vector<std::string>> ips,
                             int samples, DWORD timeoutMs);

    HANDLE              m_icmpHandle = INVALID_HANDLE_VALUE;
    QThreadPool         m_pool;
    std::atomic<int>    m_version{0};
    std::atomic<int>    m_pending{0};
};


