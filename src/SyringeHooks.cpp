#include <windows.h>

namespace
{
struct SyHookDecl
{
    unsigned address;
    unsigned size;
    const char* name;
    unsigned padding;
};

// Hook the instructions immediately after SapphireA's existing lifecycle hooks.
const SyHookDecl hooks[] __attribute__((section(".syhks00"), used, aligned(16))) =
{
    {0x55D365, 6, "SA_APMM_Update", 0},
    {0x67E6CA, 6, "SA_APMM_Reset", 0},  // Load game.
    {0x683E7C, 10, "SA_APMM_Reset", 0}, // New scenario.
};

struct SyringeHandshakeInfo
{
    int cbSize;
    int num_hooks;
    unsigned checksum;
    DWORD exeFilesize;
    DWORD exeTimestamp;
    unsigned exeCRC;
    int cchMessage;
    char* Message;
};
}

extern "C" __declspec(dllexport) HRESULT __cdecl SyringeHandshake(SyringeHandshakeInfo* info)
{
    if (!info || info->cbSize < static_cast<int>(sizeof(SyringeHandshakeInfo)))
        return E_FAIL;

    const bool supported = info->exeFilesize == 4816864 &&
        info->exeTimestamp == 0x3BDF544E && info->exeCRC == 0x1B499086;
    if (info->Message && info->cchMessage > 0)
        lstrcpynA(info->Message, supported ? "SA_APMM 1.0.0" :
            "SA_APMM requires the Mental Omega 3.3.6 game executable", info->cchMessage);
    return supported ? S_OK : S_FALSE;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
