#pragma once

#include <app/ConcreteClusterPath.h>
#include <app/data-model-provider/ActionReturnStatus.h>
#include <app/server-cluster/DefaultServerCluster.h>
#include <lib/core/CHIPError.h>
#include <lib/core/TLVReader.h>

// Manufacturer-specific cluster: transports a Zephyr LLEXT binary to the
// device in chunks, then loads and runs it. See plan/plan-detailed.md,
// Milestone 3.
//
// Not present in any ZAP file -- hand-registered on EP1 directly against
// CodegenDataModelProvider's registry (see AppTask::Init()). Chosen
// commands/attribute IDs are ours to define since nothing else claims this
// cluster ID.
class ExtensionLoaderCluster : public chip::app::DefaultServerCluster
{
public:
    static constexpr chip::ClusterId kClusterId   = 0xFFF1FC01;
    static constexpr chip::EndpointId kEndpointId = 1;

    enum class CommandId : chip::CommandId
    {
        kLoadStart = 0,
        kLoadChunk = 1,
        kLoadDone  = 2,
    };

    enum class AttributeId : chip::AttributeId
    {
        kTransferStatus = 0,
    };

    // Named to avoid colliding with chip::Protocols::InteractionModel::Status,
    // which command handlers below also need in scope.
    enum class TransferStatus : uint8_t
    {
        kIdle      = 0,
        kReceiving = 1,
        kLoaded    = 2,
        kError     = 3,
    };

    // Largest single LoadChunk payload we accept. Matter doesn't fragment a
    // single command across multiple UDP datagrams, so this must comfortably
    // fit within the ~1200-byte single-message budget alongside TLV/exchange
    // overhead.
    static constexpr size_t kMaxChunkSize = 900;

    // Largest extension binary we'll buffer in RAM before loading. Bounded
    // well below the LLEXT heap budget (app/llext.conf's CONFIG_LLEXT_HEAP_SIZE).
    static constexpr size_t kMaxExtensionSize = 64 * 1024;

    ExtensionLoaderCluster() : DefaultServerCluster({ kEndpointId, kClusterId }) {}
    ~ExtensionLoaderCluster() override;

    // ServerClusterInterface
    chip::app::DataModel::ActionReturnStatus ReadAttribute(const chip::app::DataModel::ReadAttributeRequest & request,
                                                           chip::app::AttributeValueEncoder & encoder) override;
    CHIP_ERROR Attributes(const chip::app::ConcreteClusterPath & path,
                          chip::ReadOnlyBufferBuilder<chip::app::DataModel::AttributeEntry> & builder) override;
    CHIP_ERROR AcceptedCommands(const chip::app::ConcreteClusterPath & path,
                               chip::ReadOnlyBufferBuilder<chip::app::DataModel::AcceptedCommandEntry> & builder) override;
    std::optional<chip::app::DataModel::ActionReturnStatus> InvokeCommand(const chip::app::DataModel::InvokeRequest & request,
                                                                          chip::TLV::TLVReader & input_arguments,
                                                                          chip::app::CommandHandler * handler) override;

private:
    chip::app::DataModel::ActionReturnStatus HandleLoadStart(chip::TLV::TLVReader & input);
    chip::app::DataModel::ActionReturnStatus HandleLoadChunk(chip::TLV::TLVReader & input);
    chip::app::DataModel::ActionReturnStatus HandleLoadDone();

    void SetTransferStatus(TransferStatus status);
    void ResetTransfer();

    TransferStatus mTransferStatus = TransferStatus::kIdle;
    uint8_t * mBuffer              = nullptr;
    size_t mExpectedSize           = 0;
    size_t mReceivedBytes          = 0;
    uint32_t mExpectedCrc          = 0;
};
