#pragma once

#include <cmath>
#include <string>
#include <vector>

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

class PingManager {
public:
    struct PingResult {
        int avg = -1;
        int jitter = 0;
    };

    static PingResult ping(const std::string &ip, int samples = 4, DWORD timeoutMs = 500) {
        HANDLE hIcmp = IcmpCreateFile();
        if (hIcmp == INVALID_HANDLE_VALUE) return {-1, 0};

        std::vector<int> results;
        results.reserve(samples);

        IN_ADDR addr{};
        if (inet_pton(AF_INET, ip.c_str(), &addr) == 1) {
            for (int i = 0; i < samples; ++i) {
                int r = pingOnce(hIcmp, addr, timeoutMs);
                if (r >= 0) results.push_back(r);
            }
        }
        IcmpCloseHandle(hIcmp);

        if (results.empty()) return {-1, 0};

        int sum = 0;
        int minRtt = results[0];
        int maxRtt = results[0];

        for (int v : results) {
            sum += v;
            if (v < minRtt) minRtt = v;
            if (v > maxRtt) maxRtt = v;
        }

        return {sum / static_cast<int>(results.size()), maxRtt - minRtt};
    }

private:
    static int pingOnce(HANDLE hIcmp, IN_ADDR addr, DWORD timeoutMs) {
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
};
