#include "PingScheduler.h"

#include <QPointer>
#include <QTimer>

#include <cmath>

static constexpr int   kSamples   = 4;
static constexpr DWORD kTimeoutMs = 500;

PingScheduler::PingScheduler(QObject *parent)
    : QObject(parent)
{
    m_icmpHandle = IcmpCreateFile();
    m_pool.setMaxThreadCount(qMax(4, QThread::idealThreadCount()));
}

PingScheduler::~PingScheduler() {
    cancel();
    m_pool.waitForDone();
    if (m_icmpHandle != INVALID_HANDLE_VALUE)
        IcmpCloseHandle(m_icmpHandle);
}

void PingScheduler::cancel() {
    ++m_version;
    m_pending.store(0);
}

void PingScheduler::schedule(const std::unordered_map<std::string, ServerRegion> &regions) {
    if (m_pending.load() > 0) return;
    if (m_icmpHandle == INVALID_HANDLE_VALUE) return;

    const int version = ++m_version;
    const int count   = static_cast<int>(regions.size());
    if (count == 0) return;

    m_pending.store(count);

    QPointer<PingScheduler> self(this);

    for (const auto &[code, region] : regions) {
        const std::string                               codeCopy = code;
        std::shared_ptr<const std::vector<std::string>> ips     = region.ips;
        HANDLE                                          handle   = m_icmpHandle;

        m_pool.start([self, version, codeCopy, ips, handle]() {
            Result r = pingRegion(handle, codeCopy, ips, kSamples, kTimeoutMs);

            QMetaObject::invokeMethod(self, [self, version, r]() {
                if (!self) return;

                const bool versionMatch = (version == self->m_version.load());
                if (versionMatch) {
                    emit self->resultReady(r);
                }

                if (self->m_pending.fetch_sub(1) == 1) {
                    if (versionMatch) {
                        QTimer::singleShot(0, self, [self, version]() {
                            if (self && version == self->m_version.load())
                                emit self->cycleFinished();
                        });
                    }
                }
            }, Qt::QueuedConnection);
        });
    }
}

int PingScheduler::pingOnce(HANDLE hIcmp, IN_ADDR addr, DWORD timeoutMs) {
    constexpr char sendData[] = "CSR_Ping";
    const DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 8;
    std::vector<char> replyBuffer(replySize);

    DWORD result = IcmpSendEcho(
        hIcmp,
        addr.S_un.S_addr,
        const_cast<char *>(sendData),
        static_cast<WORD>(sizeof(sendData)),
        nullptr,
        replyBuffer.data(),
        replySize,
        timeoutMs);

    if (result > 0) {
        auto *pReply = reinterpret_cast<PICMP_ECHO_REPLY>(replyBuffer.data());
        if (pReply->Status == IP_SUCCESS)
            return static_cast<int>(pReply->RoundTripTime);
    }
    return -1;
}

PingScheduler::Result PingScheduler::pingRegion(
    HANDLE hIcmp,
    const std::string &code,
    std::shared_ptr<const std::vector<std::string>> ips,
    int samples, DWORD timeoutMs)
{
    Result result;
    result.code = code;

    if (!ips || ips->empty()) return result;

    std::vector<int> rtts;
    rtts.reserve(samples);

    for (const auto &ip : *ips) {
        IN_ADDR addr{};
        if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) continue;

        for (int i = 0; i < samples; ++i) {
            const int r = pingOnce(hIcmp, addr, timeoutMs);
            if (r >= 0) rtts.push_back(r);
        }

        if (!rtts.empty()) break;
    }

    if (rtts.empty()) return result;

    double sum = 0.0;
    for (int v : rtts) sum += v;
    const double mean = sum / static_cast<double>(rtts.size());

    double variance = 0.0;
    for (int v : rtts) {
        const double diff = v - mean;
        variance += diff * diff;
    }
    variance /= static_cast<double>(rtts.size());

    result.avg    = static_cast<int>(std::round(mean));
    result.jitter = std::sqrt(variance);

    return result;
}
