#pragma once

#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
#include <platform/CHIPDeviceConfig.h>

#include <zephyr/sys/util.h>

#include <cstddef>
#include <cstdint>

// Keeps extensions across reboots. A binary goes into its slot in the
// extension_storage flash partition (see pm_static.yml), and what makes it
// valid is a small record in the settings: writing that record last means a
// power cut can't leave half a binary looking valid, and a factory reset,
// which clears the settings, invalidates the binaries along with everything
// else.
//
// Everything here must be called on the Matter thread.
namespace ExtensionStore {

inline constexpr size_t kSlotCount = 2;
inline constexpr size_t kSlotSize  = 8 * 1024;

struct Record
{
    uint32_t size;
    uint32_t crc;
    // FirmwareId() of the firmware this extension was loaded on.
    uint32_t firmwareId;
    // The second half of the UniqueID of the extension's endpoints.
    uint32_t uniqueIdSeed;
    // Restores started since the device last stayed up long enough to count
    // as stable.
    uint8_t attempts;
    uint8_t endpointCount;
    chip::EndpointId endpoints[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
};

// Flash can only be written in whole words, so buffers holding a binary are
// this size, with the rest filled with 0xFF.
inline size_t AlignedSize(size_t size)
{
    return ROUND_UP(size, sizeof(uint32_t));
}

bool ReadRecord(size_t slot, Record & record);
CHIP_ERROR WriteRecord(size_t slot, const Record & record);
void DeleteRecord(size_t slot);

// `alignedSize` as returned by AlignedSize().
int WriteBinary(size_t slot, const uint8_t * data, size_t alignedSize);
int ReadBinary(size_t slot, uint8_t * data, size_t alignedSize);

// Identifies the running firmware: a CRC of its whole image. Extensions are
// only linked against the firmware by symbol name, so loading one on a firmware
// with a different struct layout could go unnoticed and crash.
uint32_t FirmwareId();

} // namespace ExtensionStore
