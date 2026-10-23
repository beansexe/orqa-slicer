#pragma once

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <limits>
#include <memory>
#include <string>

namespace obn::orqa {
inline constexpr char session_magic[] = "OrqaDPAPI1\n";

inline bool protect_session(std::string& text)
{
    if (text.empty() || text.size() > (std::numeric_limits<DWORD>::max)()) return false;
    DATA_BLOB input{static_cast<DWORD>(text.size()), reinterpret_cast<BYTE*>(text.data())}, output{};
    if (!CryptProtectData(&input, L"Orqa community Bambu session", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) return false;
    std::unique_ptr<BYTE, decltype(&LocalFree)> owner(output.pbData, LocalFree);
    std::string protected_text(session_magic);
    protected_text.append(reinterpret_cast<const char*>(output.pbData), output.cbData);
    SecureZeroMemory(text.data(), text.size());
    text.swap(protected_text);
    return true;
}

inline bool unprotect_session(std::string& text)
{
    constexpr auto prefix = sizeof(session_magic) - 1;
    if (text.size() <= prefix || text.compare(0, prefix, session_magic) != 0 ||
        text.size() - prefix > (std::numeric_limits<DWORD>::max)()) return false;
    DATA_BLOB input{static_cast<DWORD>(text.size() - prefix), reinterpret_cast<BYTE*>(text.data() + prefix)}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return false;
    std::unique_ptr<BYTE, decltype(&LocalFree)> owner(output.pbData, LocalFree);
    text.assign(reinterpret_cast<const char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    return true;
}
} // namespace obn::orqa
#endif
