#pragma once

#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <comdef.h>
#include <netfw.h>
#include <windows.h>
#include <winsock2.h>


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
    if (inet_addr(ip.c_str()) == INADDR_NONE)
      return false;

    if (!getComScope().ok())
      return false;

    ComPtr<INetFwPolicy2> policy;
    if (FAILED(createPolicy(policy)))
      return false;

    ComPtr<INetFwRules> rules;
    if (FAILED(policy->get_Rules(&rules.p)))
      return false;

    ComPtr<INetFwRule> rule;
    if (FAILED(CoCreateInstance(__uuidof(NetFwRule), nullptr,
                                CLSCTX_INPROC_SERVER, __uuidof(INetFwRule),
                                reinterpret_cast<void **>(&rule.p))))
      return false;

    const std::wstring wIp = toWide(ip);
    const std::wstring wName = L"CSR_Block_" + wIp;

    rule->put_Name(_bstr_t(wName.c_str()));
    rule->put_Action(NET_FW_ACTION_BLOCK);
    rule->put_Direction(NET_FW_RULE_DIR_OUT);
    rule->put_Enabled(VARIANT_TRUE);
    rule->put_RemoteAddresses(_bstr_t(wIp.c_str()));

    return SUCCEEDED(rules->Add(rule.p));
  }

  static bool removeBlockRule(const std::string &ip) {
    if (!getComScope().ok())
      return false;

    ComPtr<INetFwPolicy2> policy;
    if (FAILED(createPolicy(policy)))
      return false;

    ComPtr<INetFwRules> rules;
    if (FAILED(policy->get_Rules(&rules.p)))
      return false;

    const std::wstring wName = L"CSR_Block_" + toWide(ip);
    return SUCCEEDED(rules->Remove(_bstr_t(wName.c_str())));
  }

  static bool removeAllRules() {
    if (!getComScope().ok())
      return false;

    ComPtr<INetFwPolicy2> policy;
    if (FAILED(createPolicy(policy)))
      return false;

    ComPtr<INetFwRules> rules;
    if (FAILED(policy->get_Rules(&rules.p)))
      return false;

    std::vector<std::wstring> toDelete;
    {
      ComPtr<IUnknown> enumeratorUnk;
      if (FAILED(rules->get__NewEnum(&enumeratorUnk.p)))
        return false;

      ComPtr<IEnumVARIANT> enumerator;
      if (FAILED(enumeratorUnk->QueryInterface(
              __uuidof(IEnumVARIANT),
              reinterpret_cast<void **>(&enumerator.p))))
        return false;

      VARIANT var;
      VariantInit(&var);
      ULONG fetched = 0;

      while (SUCCEEDED(enumerator->Next(1, &var, &fetched)) && fetched == 1) {
        if (var.vt == VT_DISPATCH) {
          ComPtr<INetFwRule> rule;
          if (SUCCEEDED(var.pdispVal->QueryInterface(
                  __uuidof(INetFwRule), reinterpret_cast<void **>(&rule.p)))) {
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

    bool allOk = true;
    for (const auto &name : toDelete)
      if (FAILED(rules->Remove(_bstr_t(name.c_str()))))
        allOk = false;

    return allOk;
  }

private:
  struct ComScope {
    explicit ComScope() {
      const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
      m_init = SUCCEEDED(hr);
      m_needUninit = (hr == S_OK);
    }
    ~ComScope() {
      if (m_needUninit)
        CoUninitialize();
    }
    bool ok() const { return m_init; }

  private:
    bool m_init = false;
    bool m_needUninit = false;
  };

  static ComScope &getComScope() {
    thread_local ComScope instance;
    return instance;
  }

  template <typename T> struct ComPtr {
    T *p = nullptr;
    ~ComPtr() {
      if (p)
        p->Release();
    }
    T **operator&() { return &p; }
    T *operator->() { return p; }
    explicit operator bool() const { return p != nullptr; }
  };

  static HRESULT createPolicy(ComPtr<INetFwPolicy2> &out) {
    return CoCreateInstance(__uuidof(NetFwPolicy2), nullptr,
                            CLSCTX_INPROC_SERVER, __uuidof(INetFwPolicy2),
                            reinterpret_cast<void **>(&out.p));
  }

  static std::wstring toWide(const std::string &s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), size);
    return result;
  }
};
