#pragma once

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
#include <iphlpapi.h>
#include <ipexport.h>
#include <icmpapi.h>

class PingManager {
public:
  static int ping(const std::string &ip, DWORD timeoutMs = 1000) {
    HANDLE hIcmp = IcmpCreateFile();
    if (hIcmp == INVALID_HANDLE_VALUE)
      return -1;

    const IPAddr addr = inet_addr(ip.c_str());
    if (addr == INADDR_NONE) {
      IcmpCloseHandle(hIcmp);
      return -1;
    }

    constexpr char sendData[] = "CSR_Ping";
    const DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 8;
    std::vector<char> replyBuffer(replySize);

    const DWORD result =
        IcmpSendEcho(hIcmp, addr, const_cast<char *>(sendData),
                     static_cast<WORD>(sizeof(sendData)), nullptr,
                     replyBuffer.data(), replySize, timeoutMs);

    int rtt = -1;
    if (result > 0) {
      const auto *pReply =
          reinterpret_cast<PICMP_ECHO_REPLY>(replyBuffer.data());
      if (pReply->Status == IP_SUCCESS)
        rtt = static_cast<int>(pReply->RoundTripTime);
    }

    IcmpCloseHandle(hIcmp);
    return rtt;
  }
};
