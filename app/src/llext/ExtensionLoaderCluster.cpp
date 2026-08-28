#include "ExtensionLoaderCluster.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app/AttributeValueEncoder.h>
#include <app/CommandHandler.h>
#include <app/data-model/Decode.h>
#include <app/data-model/StructDecodeIterator.h>
#include <app/server-cluster/AttributeListBuilder.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#include <zephyr/kernel.h>
#include <zephyr/llext/buf_loader.h>
#include <zephyr/llext/llext.h>
#include <zephyr/sys/crc.h>

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

} // namespace

ExtensionLoaderCluster::~ExtensionLoaderCluster()
{
    ResetTransfer();
}

void ExtensionLoaderCluster::ResetTransfer()
{
    // NOTE: does NOT free a buffer that a successful HandleLoadDone() has
    // already handed off to llext_load() (mBuffer is nulled there, without
    // freeing, once loading succeeds) -- llext may still reference that
    // memory for the loaded extension's lifetime.
    if (mBuffer != nullptr)
    {
        k_free(mBuffer);
        mBuffer = nullptr;
    }
    mExpectedSize  = 0;
    mReceivedBytes = 0;
    mExpectedCrc   = 0;
}

void ExtensionLoaderCluster::SetTransferStatus(TransferStatus status)
{
    SetAttributeValue(mTransferStatus, status, chip::to_underlying(AttributeId::kTransferStatus));
}

DataModel::ActionReturnStatus ExtensionLoaderCluster::ReadAttribute(const DataModel::ReadAttributeRequest & request,
                                                                    AttributeValueEncoder & encoder)
{
    switch (request.path.mAttributeId)
    {
    case chip::to_underlying(AttributeId::kTransferStatus):
        return encoder.Encode(static_cast<uint8_t>(mTransferStatus));
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
    };
    AttributeListBuilder listBuilder(builder);
    return listBuilder.Append(chip::Span<const DataModel::AttributeEntry>(entries), {});
}

CHIP_ERROR ExtensionLoaderCluster::AcceptedCommands(const ConcreteClusterPath & path,
                                                    chip::ReadOnlyBufferBuilder<DataModel::AcceptedCommandEntry> & builder)
{
    DataModel::AcceptedCommandEntry commands[] = {
        { chip::to_underlying(CommandId::kLoadStart), chip::BitMask<CommandQualityFlags>(), chip::Access::Privilege::kOperate },
        { chip::to_underlying(CommandId::kLoadChunk), chip::BitMask<CommandQualityFlags>(), chip::Access::Privilege::kOperate },
        { chip::to_underlying(CommandId::kLoadDone), chip::BitMask<CommandQualityFlags>(), chip::Access::Privilege::kOperate },
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

    mBuffer = static_cast<uint8_t *>(k_malloc(fields.totalSize));
    VerifyOrReturnError(mBuffer != nullptr, Status::ResourceExhausted);

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

    struct llext_buf_loader bufLoader = LLEXT_WRITABLE_BUF_LOADER(mBuffer, mExpectedSize);
    struct llext_load_param loadParam = LLEXT_LOAD_PARAM_DEFAULT;
    struct llext * ext                = nullptr;

    int rc = llext_load(&bufLoader.loader, "ext", &ext, &loadParam);
    if (rc != 0)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: llext_load failed: %d", rc);
        SetTransferStatus(TransferStatus::kError);
        return Status::Failure;
    }

    ChipLogProgress(NotSpecified, "ExtensionLoader: llext_load succeeded");

    // Calling "run" here (rather than just leaving the extension loaded)
    // proves it actually executes, not just links -- the generic
    // ExtensionLoaderCluster doesn't otherwise know what an extension does.
    rc = llext_call_fn(ext, "run");
    if (rc != 0)
    {
        ChipLogError(NotSpecified, "ExtensionLoader: llext_call_fn(\"run\") failed: %d", rc);
    }

    // Ownership of mBuffer now belongs to the loaded extension's lifetime,
    // not to us -- do not free it. See ResetTransfer()'s comment.
    mBuffer        = nullptr;
    mReceivedBytes = 0;
    mExpectedSize  = 0;
    mExpectedCrc   = 0;

    SetTransferStatus(TransferStatus::kLoaded);
    return Status::Success;
}
