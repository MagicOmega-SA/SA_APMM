#include "MatchApm.h"
#include "Patch.h"

namespace
{
constexpr unsigned ScenarioStarted = 0xA8ED5C;
constexpr unsigned LoadPhase = 0xA9FAB0;
constexpr unsigned SpecialDialog = 0xA8EDA0;
constexpr unsigned GameWindow = 0xB73550;
constexpr unsigned MessageListEdit = 0xA8BC60 + 0x19;
constexpr unsigned CompositeSurface = 0x88731C;

// Verify the expected Ares build before patching its status renderer.
constexpr unsigned AresDrawStatusRva = 0x6BAB0;
constexpr unsigned AresRenderStatusRva = 0x6BE80;
constexpr unsigned AresCheckFpsRva = 0x6BF1E;
constexpr unsigned AresFpsEnabledRva = 0xC0BB9;
using DrawStatus = void (__stdcall*)(const wchar_t*, int*, unsigned);
using RenderStatus = DWORD (__cdecl*)(void*);

MatchApm actions;
DrawStatus drawStatus = nullptr;
RenderStatus renderStatus = nullptr;
const volatile unsigned char* fpsEnabled = nullptr;

bool GameFilePath(wchar_t (&path)[MAX_PATH], const wchar_t* filename)
{
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    while (length && path[length - 1] != L'\\') --length;
    if (!length || length + lstrlenW(filename) >= MAX_PATH) return false;
    lstrcpyW(path + length, filename);
    return true;
}

void Log(const char* message)
{
    wchar_t path[MAX_PATH];
    if (!GameFilePath(path, L"SA_APMM.log")) return;
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, message, lstrlenA(message), &written, nullptr);
    CloseHandle(file);
}

bool InMatch()
{
    return *reinterpret_cast<const volatile unsigned char*>(ScenarioStarted) &&
        !*reinterpret_cast<const volatile int*>(LoadPhase) &&
        !*reinterpret_cast<const volatile int*>(SpecialDialog);
}

bool CountInput(const MSG& msg)
{
    switch (msg.message)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (msg.lParam & (1l << 30)) return false;
        switch (msg.wParam)
        {
        case VK_SHIFT:
        case VK_CONTROL:
        case VK_MENU:
        case VK_LWIN:
        case VK_RWIN:
        case VK_ESCAPE:
        case VK_RETURN:
        case VK_PAUSE:
        case VK_PROCESSKEY:
            return false;
        }
        if (msg.message == WM_SYSKEYDOWN && (msg.wParam == VK_TAB || msg.wParam == VK_F4))
            return false;
        return true;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK ObserveGameMessage(int code, WPARAM remove, LPARAM param)
{
    if (code == HC_ACTION && remove == PM_REMOVE)
    {
        const auto& msg = *reinterpret_cast<const MSG*>(param);
        if (CountInput(msg) && msg.hwnd == *reinterpret_cast<const HWND*>(GameWindow) &&
            InMatch() && !*reinterpret_cast<const volatile unsigned char*>(MessageListEdit))
            actions.Record(GetTickCount64());
    }
    return CallNextHookEx(nullptr, code, remove, param);
}

void __stdcall DrawFpsAndApm(const wchar_t* text, int* bottom, unsigned color)
{
    if (InMatch() && text && text[0] == L'F' && text[1] == L'P' &&
        text[2] == L'S' && text[3] == L':')
    {
        // Ares uses a 32-character buffer for this line. Pass a separate,
        // larger string so its original width measurement includes APM.
        constexpr int fpsLimit = 31;
        wchar_t combined[48];
        int length = 0;
        while (length < fpsLimit && text[length])
        {
            combined[length] = text[length];
            ++length;
        }
        wsprintfW(combined + length, L"   APM: %u", actions.Count(GetTickCount64()));
        drawStatus(combined, bottom, color);
        return;
    }
    drawStatus(text, bottom, color);
}

DWORD __cdecl DrawAresStatus(void* registers)
{
    const DWORD result = renderStatus(registers);
    if (*fpsEnabled || !InMatch()) return result;

    void* surface = *reinterpret_cast<void**>(CompositeSurface);
    if (!surface) return result;
    const auto table = *reinterpret_cast<const unsigned* const*>(surface);
    if (!table) return result;
    using SurfaceGetter = int (__thiscall*)(void*);
    const auto bytesPerPixel = reinterpret_cast<SurfaceGetter>(table[0x70 / 4]);
    const auto width = reinterpret_cast<SurfaceGetter>(table[0x7C / 4]);
    const auto height = reinterpret_cast<SurfaceGetter>(table[0x80 / 4]);
    if (!bytesPerPixel || !width || !height ||
        bytesPerPixel(surface) != 2 || width(surface) < 128 || height(surface) < 64)
        return result;

    wchar_t text[16];
    wsprintfW(text, L"APM: %u", actions.Count(GetTickCount64()));
    int bottom = 32;  // Ares starts its status line 32 pixels above the bottom.
    // The game's three shift pairs pack 8-bit RGB for its current surface.
    const auto shift = reinterpret_cast<const unsigned*>(0x8A0DD0);
    const unsigned color = ((255u >> (shift[1] & 31)) << (shift[0] & 31)) |
        ((255u >> (shift[3] & 31)) << (shift[2] & 31)) |
        ((255u >> (shift[5] & 31)) << (shift[4] & 31));
    drawStatus(text, &bottom, color);
    return result;
}

bool IsAresAddress(HMODULE module, unsigned address, unsigned bytes)
{
    MEMORY_BASIC_INFORMATION info;
    return VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) &&
        info.AllocationBase == module && info.State == MEM_COMMIT &&
        !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        address - reinterpret_cast<unsigned>(info.BaseAddress) + bytes <= info.RegionSize;
}

bool Install()
{
    HMODULE ares = GetModuleHandleW(L"Ares.dll");
    if (!ares) return false;
    const unsigned image = reinterpret_cast<unsigned>(ares);
    const unsigned helper = image + AresDrawStatusRva;
    const unsigned check = image + AresCheckFpsRva;
    const unsigned flag = image + AresFpsEnabledRva;
    constexpr unsigned char prologue[] = {0x83, 0xEC, 0x30, 0x8B, 0x54, 0x24, 0x34};
    const unsigned char guard[] = {0x80, 0x3D, static_cast<unsigned char>(flag),
        static_cast<unsigned char>(flag >> 8), static_cast<unsigned char>(flag >> 16),
        static_cast<unsigned char>(flag >> 24), 0x00, 0x0F, 0x84};
    if (!IsAresAddress(ares, helper, sizeof(prologue)) ||
        !IsAresAddress(ares, check, sizeof(guard)) || !IsAresAddress(ares, flag, 1) ||
        !PatchSame(reinterpret_cast<const void*>(helper), prologue, sizeof(prologue)) ||
        !PatchSame(reinterpret_cast<const void*>(check), guard, sizeof(guard)))
        return false;

    HHOOK input = SetWindowsHookExW(WH_GETMESSAGE, ObserveGameMessage, nullptr, GetCurrentThreadId());
    if (!input) return false;
    void* trampoline = PatchTrampAt(helper, sizeof(prologue), helper + sizeof(prologue));
    drawStatus = reinterpret_cast<DrawStatus>(trampoline);
    if (!trampoline || !PatchAt(helper, reinterpret_cast<void*>(DrawFpsAndApm), sizeof(prologue)))
    {
        drawStatus = nullptr;
        if (trampoline) VirtualFree(trampoline, 0, MEM_RELEASE);
        UnhookWindowsHookEx(input);
        return false;
    }
    fpsEnabled = reinterpret_cast<const volatile unsigned char*>(flag);
    return true;
}

void InstallStandaloneDraw()
{
    HMODULE ares = GetModuleHandleW(L"Ares.dll");
    const unsigned render = reinterpret_cast<unsigned>(ares) + AresRenderStatusRva;
    constexpr unsigned char prologue[] = {
        0x83, 0xEC, 0x50, 0x56, 0xC7, 0x44, 0x24, 0x08, 0x20, 0x00, 0x00, 0x00};
    if (!ares || !IsAresAddress(ares, render, sizeof(prologue)) ||
        !PatchSame(reinterpret_cast<const void*>(render), prologue, sizeof(prologue)))
        return;

    void* trampoline = PatchTrampAt(render, sizeof(prologue), render + sizeof(prologue));
    if (!trampoline) return;
    renderStatus = reinterpret_cast<RenderStatus>(trampoline);
    if (!PatchAt(render, reinterpret_cast<void*>(DrawAresStatus), sizeof(prologue)))
    {
        renderStatus = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
    }
}
}

extern "C" __declspec(dllexport) DWORD __cdecl SA_APMM_Reset(void*)
{
    actions.Reset();
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl SA_APMM_Update(void*)
{
    static bool initialized = false;
    if (initialized || !InMatch()) return 0;
    initialized = true;

    wchar_t path[MAX_PATH];
    if (!GameFilePath(path, L"SA_APMM.ini") ||
        !GetPrivateProfileIntW(L"MatchAPM", L"Enable", 1, path))
        return 0;

    if (!Install())
    {
        Log("apm: game input or Ares status unavailable\r\n");
        return 0;
    }
    InstallStandaloneDraw();
    Log("apm: game input and Ares status ON\r\n");
    if (!renderStatus) Log("apm: standalone status unavailable\r\n");
    return 0;
}
