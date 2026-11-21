#pragma once

#include <windows.h>

inline bool PatchSame(const void* address, const unsigned char* expected, unsigned length)
{
    const auto bytes = static_cast<const unsigned char*>(address);
    for (unsigned i = 0; i < length; ++i)
        if (bytes[i] != expected[i]) return false;
    return true;
}

inline void WriteJump(unsigned char* entry, unsigned target)
{
    const unsigned offset = target - (reinterpret_cast<unsigned>(entry) + 5);
    entry[0] = 0xE9;
    for (unsigned i = 0; i < 4; ++i)
        entry[i + 1] = static_cast<unsigned char>(offset >> (8 * i));
}

// The verified prologues contain complete instructions with no relative operands.
inline void* PatchTrampAt(unsigned address, unsigned length, unsigned continuation)
{
    auto bytes = static_cast<unsigned char*>(VirtualAlloc(nullptr, length + 5,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!bytes) return nullptr;
    const auto source = reinterpret_cast<const unsigned char*>(address);
    for (unsigned i = 0; i < length; ++i) bytes[i] = source[i];
    WriteJump(bytes + length, continuation);
    FlushInstructionCache(GetCurrentProcess(), bytes, length + 5);
    return bytes;
}

inline bool PatchAt(unsigned address, void* target, unsigned length)
{
    auto bytes = reinterpret_cast<unsigned char*>(address);
    DWORD protection;
    if (!VirtualProtect(bytes, length, PAGE_EXECUTE_READWRITE, &protection)) return false;
    WriteJump(bytes, reinterpret_cast<unsigned>(target));
    for (unsigned i = 5; i < length; ++i) bytes[i] = 0x90;
    VirtualProtect(bytes, length, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), bytes, length);
    return true;
}
