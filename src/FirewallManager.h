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
#include <windows.h>
#include <comdef.h>
#include <netfw.h>
#include <wrl/client.h>

class FirewallManager {
public:
    static bool isAdmin() {
        BOOL elevated = FALSE;
        HANDLE hToken = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
            TOKEN_ELEVATION elev{};
            DWORD size = sizeof(elev);
            if (GetTokenInformation(hToken, TokenElevation, &elev, size, &size))
                elevated = elev.TokenIsElevated;
            CloseHandle(hToken);
        }
        return elevated != FALSE;
    }

    static bool addBlockRule(const std::string &ip) {
        IN_ADDR dummy{};
        if (inet_pton(AF_INET, ip.c_str(), &dummy) != 1) return false;

        ComScope scope;
        if (!scope.ok()) return false;

        Microsoft::WRL::ComPtr<INetFwPolicy2> policy;
        if (FAILED(createPolicy(policy))) return false;

        Microsoft::WRL::ComPtr<INetFwRules> rules;
        if (FAILED(policy->get_Rules(&rules))) return false;

        Microsoft::WRL::ComPtr<INetFwRule> rule;
        if (FAILED(CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rule))))
            return false;

        std::wstring wIp   = toWide(ip);
        std::wstring wName = L"CSR_Block_" + wIp;

        rule->put_Name(_bstr_t(wName.c_str()));
        rule->put_Action(NET_FW_ACTION_BLOCK);
        rule->put_Direction(NET_FW_RULE_DIR_OUT);
        rule->put_Enabled(VARIANT_TRUE);
        rule->put_RemoteAddresses(_bstr_t(wIp.c_str()));

        return SUCCEEDED(rules->Add(rule.Get()));
    }

    static bool removeBlockRule(const std::string &ip) {
        ComScope scope;
        if (!scope.ok()) return false;

        Microsoft::WRL::ComPtr<INetFwPolicy2> policy;
        if (FAILED(createPolicy(policy))) return false;

        Microsoft::WRL::ComPtr<INetFwRules> rules;
        if (FAILED(policy->get_Rules(&rules))) return false;

        std::wstring wName = L"CSR_Block_" + toWide(ip);
        return SUCCEEDED(rules->Remove(_bstr_t(wName.c_str())));
    }

    static bool removeAllRules() {
        ComScope scope;
        if (!scope.ok()) return false;

        Microsoft::WRL::ComPtr<INetFwPolicy2> policy;
        if (FAILED(createPolicy(policy))) return false;

        Microsoft::WRL::ComPtr<INetFwRules> rules;
        if (FAILED(policy->get_Rules(&rules))) return false;

        std::vector<std::wstring> toDelete;
        Microsoft::WRL::ComPtr<IUnknown> enumeratorUnk;
        if (SUCCEEDED(rules->get__NewEnum(&enumeratorUnk))) {
            Microsoft::WRL::ComPtr<IEnumVARIANT> enumerator;
            if (SUCCEEDED(enumeratorUnk.As(&enumerator))) {
                VARIANT var;
                VariantInit(&var);
                ULONG fetched = 0;
                while (SUCCEEDED(enumerator->Next(1, &var, &fetched)) && fetched == 1) {
                    if (var.vt == VT_DISPATCH) {
                        Microsoft::WRL::ComPtr<INetFwRule> rule;
                        if (SUCCEEDED(var.pdispVal->QueryInterface(IID_PPV_ARGS(&rule)))) {
                            BSTR bName = nullptr;
                            if (SUCCEEDED(rule->get_Name(&bName)) && bName) {
                                std::wstring name(bName, SysStringLen(bName));
                                if (name.rfind(L"CSR_Block_", 0) == 0)
                                    toDelete.push_back(std::move(name));
                                SysFreeString(bName);
                            }
                        }
                    }
                    VariantClear(&var);
                }
            }
        }

        bool allOk = true;
        for (const auto &name : toDelete)
            if (FAILED(rules->Remove(_bstr_t(name.c_str())))) allOk = false;

        return allOk;
    }

private:
    struct ComScope {
        ComScope() {
            HRESULT hr  = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            m_ok        = SUCCEEDED(hr);
            m_needUninit = (hr == S_OK);
        }
        ~ComScope() { if (m_needUninit) CoUninitialize(); }
        bool ok() const { return m_ok; }
    private:
        bool m_ok        = false;
        bool m_needUninit = false;
    };

    static HRESULT createPolicy(Microsoft::WRL::ComPtr<INetFwPolicy2> &out) {
        return CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&out));
    }

    static std::wstring toWide(const std::string &s) {
        if (s.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring result(size - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), size);
        return result;
    }
};
