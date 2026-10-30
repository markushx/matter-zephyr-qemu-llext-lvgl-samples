#include "ExtensionLoaderCluster.h"

#include "../dynamic/DynamicEndpointManager.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app/AttributeValueEncoder.h>
#include <app/CommandHandler.h>
#include <app/data-model/Decode.h>
#include <app/data-model/Nullable.h>
#include <app/data-model/StructDecodeIterator.h>
#include <app/server-cluster/AttributeListBuilder.h>
#include <crypto/RandUtils.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <zephyr/kernel.h>
#include <zephyr/llext/buf_loader.h>
#include <zephyr/llext/llext.h>
#include <zephyr/sys/crc.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

using namespace chip::app;
using namespace chip::app::DataModel;
using Status = chip::Protocols::InteractionModel::Status;

namespace {

// Hand-written field decoders, matching the same shape ZAP-codegen produces
// (see zzz_generated/.../Commands.ipp) -- there's no ZAP entry for this
// cluster at all, so these are written by hand instead of generated.

struct LoadStartFields
{
    uint32_t totalSize = 0;
    uint32_t crc32      = 0;

    CHIP_ERROR Decode(chip::TLV::TLVReader & reader)
    {
        Clusters::detail::StructDecodeIterator iterator(reader);
        while (true)
        {
            uint8_t contextTag = 0;
            CHIP_ERROR err     = iterator.Next(contextTag);
            VerifyOrReturnError(err != CHIP_ERROR_END_OF_TLV, CHIP_NO_ERROR);
            ReturnErrorOnFailure(err);

            if (contextTag == 0)
            {
                err = DataModel::Decode(reader, totalSize);
            }
            else if (contextTag == 1)
            {
                err = DataModel::Decode(reader, crc32);
            }
            ReturnErrorOnFailure(err);
        }
    }
};

struct LoadChunkFields
{
    uint32_t offset = 0;
    chip::ByteSpan data;

    CHIP_ERROR Decode(chip::TLV::TLVReader & reader)
    {
        Clusters::detail::StructDecodeIterator iterator(reader);
        while (true)
        {
            uint8_t contextTag = 0;
            CHIP_ERROR err     = iterator.Next(contextTag);
            VerifyOrReturnError(err != CHIP_ERROR_END_OF_TLV, CHIP_NO_ERROR);
            ReturnErrorOnFailure(err);

            if (contextTag == 0)
            {
                err = DataModel::Decode(reader, offset);
            }
            else if (contextTag == 1)
            {
                err = DataModel::Decode(reader, data);
            }
            ReturnErrorOnFailure(err);
        }
    }
};

struct UnloadFields
{
    uint8_t slot = 0;

    CHIP_ERROR Decode(chip::TLV::TLVReader & reader)
    {
        Clusters::detail::StructDecodeIterator iterator(reader);
        while (true)
        {
            uint8_t contextTag = 0;
            CHIP_ERROR err     = iterator.Next(contextTag);
            VerifyOrReturnError(err != CHIP_ERROR_END_OF_TLV, CHIP_NO_ERROR);
            ReturnErrorOnFailure(err);

            if (contextTag == 0)
            {
                err = DataModel::Decode(reader, slot);
            }
            ReturnErrorOnFailure(err);
        }
    }
};

} // namespace

ExtensionLoaderCluster::~ExtensionLoaderCluster()
{
    ResetTransfer();
    for (Slot & slot : mSlots)
    {
        UnloadExtension(slot);
    }
    if (mPollTimerRunning)
    {
        chip::DeviceLayer::SystemLayer().CancelTimer(OnPollTimer, this);
    }
}

void ExtensionLoaderCluster::ResetTransfer()
{
    // NOTE: does NOT free a buffer that a successful HandleLoadDone() has
    // already handed off to llext_load() (it moves to the slot's buffer
    // there, without freeing, once loading succeeds) -- llext still references
    // that memory for the loaded extension's lifetime. UnloadExtension() frees
    // it.
    if (mBuffer != nullptr)
    {
        k_free(mBuffer);
        mBuffer = nullptr;
    }
    mExpectedSize  = 0;
    mReceivedBytes = 0;
    mExpectedCrc   = 0;
}

void ExtensionLoaderCluster::UnloadExtension(Slot & slot)
{
    if (slot.ext == nullptr)
    {
        return;
    }

    // First take away everything that points into the extension: its
    // endpoints (whose attribute and command callbacks are its code, and
    // whose descriptions are its data) and its poll function.
    DynamicEndpointManager::RemoveAll(&slot);
    slot.poll = nullptr;

    int rc = llext_unload(&slot.ext);
    if (rc != 0)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: llext_unload failed: %d", rc);
    }
    slot.ext = nullptr;

    k_free(slot.buffer);
    slot.buffer = nullptr;

    NotifyAttributeChanged(chip::to_underlying(AttributeId::kLoadedExtensions));
}

uint8_t ExtensionLoaderCluster::LoadedExtensions() const
{
    uint8_t loaded = 0;
    for (size_t i = 0; i < kMaxExtensions; i++)
    {
        loaded |= static_cast<uint8_t>((mSlots[i].ext != nullptr ? 1 : 0) << i);
    }
    return loaded;
}

void ExtensionLoaderCluster::OnPollTimer(chip::System::Layer * layer, void * context)
{
    auto * self = static_cast<ExtensionLoaderCluster *>(context);

    for (Slot & slot : self->mSlots)
    {
        if (slot.poll != nullptr)
        {
            DynamicEndpointManager::SetCurrentOwner(&slot, slot.uniqueIdSeed);
            slot.poll();
            DynamicEndpointManager::SetCurrentOwner(nullptr);
        }
    }

    // Keep ticking only while something is loaded.
    self->mPollTimerRunning = self->LoadedExtensions() != 0 && layer->StartTimer(kPollInterval, OnPollTimer, self) == CHIP_NO_ERROR;
}

void ExtensionLoaderCluster::OnRestoreStable(chip::System::Layer * layer, void * context)
{
    auto * self = static_cast<ExtensionLoaderCluster *>(context);

    for (size_t i = 0; i < kMaxExtensions; i++)
    {
        ExtensionStore::Record record;
        if (self->mSlots[i].ext != nullptr && ExtensionStore::ReadRecord(i, record) && record.attempts != 0)
        {
            record.attempts = 0;
            LogErrorOnFailure(ExtensionStore::WriteRecord(i, record));
        }
    }
}

void ExtensionLoaderCluster::SetDisabled(size_t slotIndex, bool disabled)
{
    const uint8_t bit    = static_cast<uint8_t>(1 << slotIndex);
    const uint8_t bitmap = disabled ? (mDisabledExtensions | bit) : (mDisabledExtensions & ~bit);
    VerifyOrReturn(mDisabledExtensions != bitmap);
    mDisabledExtensions = bitmap;
    NotifyAttributeChanged(chip::to_underlying(AttributeId::kDisabledExtensions));
}

void ExtensionLoaderCluster::SetTransferStatus(TransferStatus status)
{
    VerifyOrReturn(mTransferStatus != status);
    mTransferStatus = status;
    NotifyAttributeChanged(chip::to_underlying(AttributeId::kTransferStatus));
}

DataModel::ActionReturnStatus ExtensionLoaderCluster::ReadAttribute(const DataModel::ReadAttributeRequest & request,
                                                                    AttributeValueEncoder & encoder)
{
    switch (request.path.mAttributeId)
    {
    case chip::to_underlying(AttributeId::kTransferStatus):
        return encoder.Encode(static_cast<uint8_t>(mTransferStatus));
    case chip::to_underlying(AttributeId::kLoadedExtensions):
        return encoder.Encode(LoadedExtensions());
    case chip::to_underlying(AttributeId::kDisabledExtensions):
        return encoder.Encode(mDisabledExtensions);
    case Clusters::Globals::Attributes::FeatureMap::Id:
        return encoder.Encode(static_cast<uint32_t>(0));
    case Clusters::Globals::Attributes::ClusterRevision::Id:
        return encoder.Encode(static_cast<uint16_t>(1));
    default:
        return Status::UnsupportedAttribute;
    }
}

CHIP_ERROR ExtensionLoaderCluster::Attributes(const ConcreteClusterPath & path,
                                              chip::ReadOnlyBufferBuilder<DataModel::AttributeEntry> & builder)
{
    DataModel::AttributeEntry entries[] = {
        { chip::to_underlying(AttributeId::kTransferStatus), chip::BitMask<AttributeQualityFlags>(), chip::Access::Privilege::kView,
          std::nullopt },
        { chip::to_underlying(AttributeId::kLoadedExtensions), chip::BitMask<AttributeQualityFlags>(), chip::Access::Privilege::kView,
          std::nullopt },
        { chip::to_underlying(AttributeId::kDisabledExtensions), chip::BitMask<AttributeQualityFlags>(),
          chip::Access::Privilege::kView, std::nullopt },
    };
    AttributeListBuilder listBuilder(builder);
    return listBuilder.Append(chip::Span<const DataModel::AttributeEntry>(entries), {});
}

CHIP_ERROR ExtensionLoaderCluster::AcceptedCommands(const ConcreteClusterPath & path,
                                                    chip::ReadOnlyBufferBuilder<DataModel::AcceptedCommandEntry> & builder)
{
    DataModel::AcceptedCommandEntry commands[] = {
        { chip::to_underlying(CommandId::kLoadStart), chip::BitMask<CommandQualityFlags>(), chip::Access::Privilege::kAdminister },
        { chip::to_underlying(CommandId::kLoadChunk), chip::BitMask<CommandQualityFlags>(), chip::Access::Privilege::kAdminister },
        { chip::to_underlying(CommandId::kLoadDone), chip::BitMask<CommandQualityFlags>(), chip::Access::Privilege::kAdminister },
        { chip::to_underlying(CommandId::kUnload), chip::BitMask<CommandQualityFlags>(), chip::Access::Privilege::kAdminister },
    };
    return builder.AppendElements(chip::Span<DataModel::AcceptedCommandEntry>(commands));
}

std::optional<DataModel::ActionReturnStatus> ExtensionLoaderCluster::InvokeCommand(const DataModel::InvokeRequest & request,
                                                                                   chip::TLV::TLVReader & input_arguments,
                                                                                   CommandHandler * handler)
{
    switch (request.path.mCommandId)
    {
    case chip::to_underlying(CommandId::kLoadStart):
        return HandleLoadStart(input_arguments);
    case chip::to_underlying(CommandId::kLoadChunk):
        return HandleLoadChunk(input_arguments);
    case chip::to_underlying(CommandId::kLoadDone):
        return HandleLoadDone();
    case chip::to_underlying(CommandId::kUnload):
        return HandleUnload(input_arguments);
    default:
        return Status::UnsupportedCommand;
    }
}

DataModel::ActionReturnStatus ExtensionLoaderCluster::HandleLoadStart(chip::TLV::TLVReader & input)
{
    LoadStartFields fields;
    VerifyOrReturnError(fields.Decode(input) == CHIP_NO_ERROR, Status::InvalidCommand);
    VerifyOrReturnError(fields.totalSize > 0 && fields.totalSize <= kMaxExtensionSize, Status::ConstraintError);

    ResetTransfer();

    const size_t alignedSize = ExtensionStore::AlignedSize(fields.totalSize);
    mBuffer                  = static_cast<uint8_t *>(k_malloc(alignedSize));
    VerifyOrReturnError(mBuffer != nullptr, Status::ResourceExhausted);
    memset(mBuffer + fields.totalSize, 0xFF, alignedSize - fields.totalSize);

    mExpectedSize = fields.totalSize;
    mExpectedCrc  = fields.crc32;
    ChipLogProgress(NotSpecified, "ExtensionLoader: LoadStart size=%lu crc=0x%08lx", static_cast<unsigned long>(mExpectedSize),
                    static_cast<unsigned long>(mExpectedCrc));
    SetTransferStatus(TransferStatus::kReceiving);
    return Status::Success;
}

DataModel::ActionReturnStatus ExtensionLoaderCluster::HandleLoadChunk(chip::TLV::TLVReader & input)
{
    VerifyOrReturnError(mTransferStatus == TransferStatus::kReceiving && mBuffer != nullptr, Status::InvalidInState);

    LoadChunkFields fields;
    VerifyOrReturnError(fields.Decode(input) == CHIP_NO_ERROR, Status::InvalidCommand);
    VerifyOrReturnError(fields.data.size() <= kMaxChunkSize, Status::ConstraintError);
    VerifyOrReturnError(static_cast<uint64_t>(fields.offset) + fields.data.size() <= mExpectedSize, Status::ConstraintError);

    memcpy(mBuffer + fields.offset, fields.data.data(), fields.data.size());

    // Track the highest byte offset written so far, not a running total --
    // safe against duplicate/out-of-order chunk delivery.
    size_t coveredUpTo = static_cast<size_t>(fields.offset) + fields.data.size();
    if (coveredUpTo > mReceivedBytes)
    {
        mReceivedBytes = coveredUpTo;
    }

    return Status::Success;
}

DataModel::ActionReturnStatus ExtensionLoaderCluster::HandleLoadDone()
{
    VerifyOrReturnError(mTransferStatus == TransferStatus::kReceiving && mBuffer != nullptr, Status::InvalidInState);

    if (mReceivedBytes != mExpectedSize)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: LoadDone with incomplete transfer (%lu/%lu bytes)",
                     static_cast<unsigned long>(mReceivedBytes), static_cast<unsigned long>(mExpectedSize));
        SetTransferStatus(TransferStatus::kError);
        return Status::InvalidInState;
    }

    uint32_t actualCrc = crc32_ieee(mBuffer, mExpectedSize);
    if (actualCrc != mExpectedCrc)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: CRC mismatch: expected 0x%08lx, got 0x%08lx",
                     static_cast<unsigned long>(mExpectedCrc), static_cast<unsigned long>(actualCrc));
        SetTransferStatus(TransferStatus::kError);
        return Status::Failure;
    }

    size_t slotIndex = 0;
    while (slotIndex < kMaxExtensions && mSlots[slotIndex].ext != nullptr)
    {
        slotIndex++;
    }
    if (slotIndex == kMaxExtensions)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: all %u slots in use, unload an extension first",
                     static_cast<unsigned>(kMaxExtensions));
        SetTransferStatus(TransferStatus::kError);
        return Status::ResourceExhausted;
    }

    // A disabled extension may still be stored here. This replaces it.
    ExtensionStore::DeleteRecord(slotIndex);
    SetDisabled(slotIndex, false);

    // Store it now, because llext_load() relocates the binary in its buffer.
    // It only counts as stored once the record is written below, after it ran:
    // one that crashes right away is simply gone after the reset.
    int rc = ExtensionStore::WriteBinary(slotIndex, mBuffer, ExtensionStore::AlignedSize(mExpectedSize));
    if (rc != 0)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: storing the extension failed: %d", rc);
        SetTransferStatus(TransferStatus::kError);
        return Status::Failure;
    }

    ExtensionStore::Record record = {};
    record.size                   = static_cast<uint32_t>(mExpectedSize);
    record.crc                    = mExpectedCrc;
    record.firmwareId             = ExtensionStore::FirmwareId();
    record.uniqueIdSeed           = chip::Crypto::GetRandU32();

    rc = LoadIntoSlot(slotIndex, mBuffer, mExpectedSize, record.uniqueIdSeed, {});
    if (rc != 0)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: llext_load failed: %d", rc);
        SetTransferStatus(TransferStatus::kError);
        return Status::Failure;
    }

    // Ownership of mBuffer now belongs to the loaded extension's lifetime,
    // not to the transfer -- do not free it. See ResetTransfer()'s comment.
    mBuffer        = nullptr;
    mReceivedBytes = 0;
    mExpectedSize  = 0;
    mExpectedCrc   = 0;

    record.endpointCount = static_cast<uint8_t>(
        DynamicEndpointManager::GetEndpoints(&mSlots[slotIndex], record.endpoints, MATTER_ARRAY_SIZE(record.endpoints)));
    CHIP_ERROR err = ExtensionStore::WriteRecord(slotIndex, record);
    if (err != CHIP_NO_ERROR)
    {
        // Don't leave it running while it looks like it will come back.
        ChipLogError(NotSpecified, "ExtensionLoader: storing the record failed: %" CHIP_ERROR_FORMAT, err.Format());
        UnloadExtension(mSlots[slotIndex]);
        SetTransferStatus(TransferStatus::kError);
        return Status::Failure;
    }

    SetTransferStatus(TransferStatus::kLoaded);
    return Status::Success;
}

int ExtensionLoaderCluster::LoadIntoSlot(size_t slotIndex, uint8_t * buffer, size_t size, uint32_t uniqueIdSeed,
                                         chip::Span<const chip::EndpointId> restoredEndpoints)
{
    Slot & slot = mSlots[slotIndex];

    // llext identifies extensions by name: loading under a name that is
    // already in use would just bump that extension's use count instead of
    // loading the new binary. One name per slot.
    char name[8];
    snprintf(name, sizeof(name), "ext%u", static_cast<unsigned>(slotIndex));

    struct llext_buf_loader bufLoader = LLEXT_WRITABLE_BUF_LOADER(buffer, size);
    struct llext_load_param loadParam = LLEXT_LOAD_PARAM_DEFAULT;
    struct llext * ext                = nullptr;

    int rc = llext_load(&bufLoader.loader, name, &ext, &loadParam);
    VerifyOrReturnValue(rc == 0, rc);

    ChipLogProgress(NotSpecified, "ExtensionLoader: loaded extension into slot %u", static_cast<unsigned>(slotIndex));

    slot.ext          = ext;
    slot.buffer       = buffer;
    slot.uniqueIdSeed = uniqueIdSeed;

    // "run" is the extension's entry point, where it adds its endpoints.
    // Whatever it adds belongs to this slot and goes away with it.
    auto run = reinterpret_cast<ExtensionFn>(const_cast<void *>(llext_find_sym(&ext->exp_tab, kRunSymbol)));
    if (run != nullptr)
    {
        DynamicEndpointManager::SetCurrentOwner(&slot, uniqueIdSeed);
        DynamicEndpointManager::SetRestoredEndpoints(restoredEndpoints);
        run();
        DynamicEndpointManager::SetCurrentOwner(nullptr);
    }

    slot.poll = reinterpret_cast<ExtensionFn>(const_cast<void *>(llext_find_sym(&ext->exp_tab, kPollSymbol)));
    if (!mPollTimerRunning)
    {
        mPollTimerRunning = chip::DeviceLayer::SystemLayer().StartTimer(kPollInterval, OnPollTimer, this) == CHIP_NO_ERROR;
    }

    NotifyAttributeChanged(chip::to_underlying(AttributeId::kLoadedExtensions));
    return 0;
}

bool ExtensionLoaderCluster::RestoreSlot(size_t slotIndex, ExtensionStore::Record & record, uint32_t firmwareId)
{
    if (record.size == 0 || record.size > kMaxExtensionSize || record.endpointCount > MATTER_ARRAY_SIZE(record.endpoints))
    {
        ChipLogError(NotSpecified, "ExtensionLoader: slot %u: invalid record", static_cast<unsigned>(slotIndex));
        return false;
    }
    if (record.firmwareId != firmwareId)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: slot %u: stored on another firmware, send it again",
                     static_cast<unsigned>(slotIndex));
        return false;
    }
    if (record.attempts >= kMaxRestoreAttempts)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: slot %u: disabled after %u failed restores, send it again",
                     static_cast<unsigned>(slotIndex), record.attempts);
        return false;
    }

    // Counted before running anything of it: if it takes the device down,
    // nothing runs afterwards that could count it.
    record.attempts++;
    if (ExtensionStore::WriteRecord(slotIndex, record) != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: slot %u: can't count the attempt, not restoring",
                     static_cast<unsigned>(slotIndex));
        return false;
    }

    const size_t alignedSize = ExtensionStore::AlignedSize(record.size);
    auto * buffer            = static_cast<uint8_t *>(k_malloc(alignedSize));
    if (buffer == nullptr)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: slot %u: out of memory", static_cast<unsigned>(slotIndex));
        return false;
    }

    int rc = ExtensionStore::ReadBinary(slotIndex, buffer, alignedSize);
    if (rc == 0 && crc32_ieee(buffer, record.size) != record.crc)
    {
        rc = -EBADMSG;
    }
    if (rc == 0)
    {
        rc = LoadIntoSlot(slotIndex, buffer, record.size, record.uniqueIdSeed,
                          chip::Span<const chip::EndpointId>(record.endpoints, record.endpointCount));
    }
    if (rc != 0)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: slot %u: restoring failed: %d", static_cast<unsigned>(slotIndex), rc);
        k_free(buffer);
        return false;
    }
    return true;
}

void ExtensionLoaderCluster::RestoreExtensions()
{
    uint32_t firmwareId = 0;
    bool restored       = false;

    for (size_t i = 0; i < kMaxExtensions; i++)
    {
        ExtensionStore::Record record;
        if (!ExtensionStore::ReadRecord(i, record))
        {
            continue;
        }
        if (firmwareId == 0)
        {
            firmwareId = ExtensionStore::FirmwareId();
        }
        if (RestoreSlot(i, record, firmwareId))
        {
            restored = true;
        }
        else
        {
            SetDisabled(i, true);
        }
    }

    if (restored)
    {
        LogErrorOnFailure(chip::DeviceLayer::SystemLayer().StartTimer(kRestoreStablePeriod, OnRestoreStable, this));
    }
}

DataModel::ActionReturnStatus ExtensionLoaderCluster::HandleUnload(chip::TLV::TLVReader & input)
{
    UnloadFields fields;
    VerifyOrReturnError(fields.Decode(input) == CHIP_NO_ERROR, Status::InvalidCommand);
    VerifyOrReturnError(fields.slot < kMaxExtensions, Status::ConstraintError);
    VerifyOrReturnError(mSlots[fields.slot].ext != nullptr || (mDisabledExtensions & (1 << fields.slot)) != 0, Status::NotFound);

    UnloadExtension(mSlots[fields.slot]);
    ExtensionStore::DeleteRecord(fields.slot);
    SetDisabled(fields.slot, false);
    ChipLogProgress(NotSpecified, "ExtensionLoader: unloaded slot %u", fields.slot);
    return Status::Success;
}
