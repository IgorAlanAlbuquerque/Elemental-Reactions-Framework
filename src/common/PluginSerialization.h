#pragma once

#include <cstdint>
#include <vector>

#include "SKSE/SKSE.h"

namespace Ser {

    using SaveFn = bool (*)(SKSE::SerializationInterface*, bool dryRun);
    using LoadFn = bool (*)(SKSE::SerializationInterface*, std::uint32_t, std::uint32_t);
    using RevertFn = void (*)();

    struct Handler {
        std::uint32_t recordID;
        std::uint32_t version;
        SaveFn save;
        LoadFn load;
        RevertFn revert;
    };

    void Register(const Handler& h);

    void Install(std::uint32_t uniqueID);
}

namespace SerFunctions {
    void OnSave(SKSE::SerializationInterface* ser);
    void OnLoad(SKSE::SerializationInterface* ser);
    void OnRevert(SKSE::SerializationInterface* ser);
}

template <class T>
inline bool SerWrite(SKSE::SerializationInterface* ser, const T& pod) noexcept
    requires(std::is_trivially_copyable_v<T>)
{
    return ser->WriteRecordData(std::addressof(pod), static_cast<std::uint32_t>(sizeof(T)));
}

template <class T>
inline bool SerRead(SKSE::SerializationInterface* ser, T& pod) noexcept
    requires(std::is_trivially_copyable_v<T>)
{
    return ser->ReadRecordData(std::addressof(pod), static_cast<std::uint32_t>(sizeof(T)));
}

template <class T>
inline bool SerWriteVec(SKSE::SerializationInterface* ser, const std::vector<T>& v) noexcept
    requires(std::is_trivially_copyable_v<T>)
{
    const auto n = static_cast<std::uint32_t>(v.size());
    if (!SerWrite(ser, n)) return false;
    if (n == 0) return true;
    return ser->WriteRecordData(v.data(), n * static_cast<std::uint32_t>(sizeof(T)));
}

template <class T>
inline bool SerReadVec(SKSE::SerializationInterface* ser, std::vector<T>& v) noexcept
    requires(std::is_trivially_copyable_v<T>)
{
    std::uint32_t n{};
    if (!SerRead(ser, n)) return false;
    v.resize(n);
    if (n == 0) return true;
    return ser->ReadRecordData(v.data(), n * static_cast<std::uint32_t>(sizeof(T)));
}
