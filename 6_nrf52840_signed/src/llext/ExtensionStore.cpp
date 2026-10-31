#include "ExtensionStore.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/KeyValueStoreManager.h>

#include <pm_config.h>

#include <zephyr/linker/linker-defs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>

#include <cstdio>

using chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr;

namespace {

static_assert(PM_EXTENSION_STORAGE_SIZE >= ExtensionStore::kSlotCount * ExtensionStore::kSlotSize);

// Part of the key, so that a record with another layout is simply not found.
// 1 is what 5_nrf52840_thread/ stores, for unsigned extensions.
constexpr unsigned kRecordVersion = 2;

void RecordKey(size_t slot, char (&key)[16])
{
    snprintf(key, sizeof(key), "llext/r%u/%u", kRecordVersion, static_cast<unsigned>(slot));
}

} // namespace

namespace ExtensionStore {

bool ReadRecord(size_t slot, Record & record)
{
    char key[16];
    RecordKey(slot, key);

    size_t readSize = 0;
    CHIP_ERROR err  = KeyValueStoreMgr().Get(key, &record, sizeof(record), &readSize);
    return err == CHIP_NO_ERROR && readSize == sizeof(record);
}

CHIP_ERROR WriteRecord(size_t slot, const Record & record)
{
    char key[16];
    RecordKey(slot, key);
    return KeyValueStoreMgr().Put(key, &record, sizeof(record));
}

void DeleteRecord(size_t slot)
{
    char key[16];
    RecordKey(slot, key);

    CHIP_ERROR err = KeyValueStoreMgr().Delete(key);
    if (err != CHIP_NO_ERROR && err != CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND)
    {
        ChipLogError(NotSpecified, "ExtensionStore: deleting record %u failed: %" CHIP_ERROR_FORMAT, static_cast<unsigned>(slot),
                     err.Format());
    }
}

int WriteBinary(size_t slot, const uint8_t * data, size_t alignedSize)
{
    VerifyOrReturnValue(slot < kSlotCount && alignedSize <= kSlotSize, -EINVAL);

    const struct flash_area * area = nullptr;
    int rc                         = flash_area_open(PM_EXTENSION_STORAGE_ID, &area);
    VerifyOrReturnValue(rc == 0, rc);

    rc = flash_area_erase(area, slot * kSlotSize, kSlotSize);
    if (rc == 0)
    {
        rc = flash_area_write(area, slot * kSlotSize, data, alignedSize);
    }
    flash_area_close(area);
    return rc;
}

int ReadBinary(size_t slot, uint8_t * data, size_t alignedSize)
{
    VerifyOrReturnValue(slot < kSlotCount && alignedSize <= kSlotSize, -EINVAL);

    const struct flash_area * area = nullptr;
    int rc                         = flash_area_open(PM_EXTENSION_STORAGE_ID, &area);
    VerifyOrReturnValue(rc == 0, rc);

    rc = flash_area_read(area, slot * kSlotSize, data, alignedSize);
    flash_area_close(area);
    return rc;
}

uint32_t FirmwareId()
{
    return crc32_ieee(reinterpret_cast<const uint8_t *>(__rom_region_start), reinterpret_cast<size_t>(_flash_used));
}

} // namespace ExtensionStore
