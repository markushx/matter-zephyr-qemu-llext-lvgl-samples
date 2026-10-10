#pragma once

#include <app/ConcreteClusterPath.h>
#include <app/data-model-provider/ActionReturnStatus.h>
#include <app/server-cluster/DefaultServerCluster.h>
#include <lib/core/CHIPError.h>
#include <lib/core/TLVReader.h>
#include <system/SystemClock.h>
#include <system/SystemLayer.h>

struct llext;

// Manufacturer-specific cluster: transports a Zephyr LLEXT binary to the
// device in chunks, then loads and runs it. See plan/plan-detailed.md,
// Milestone 3.
//
// Unlike in the 2_llext/ and 3_llext_lvgl/ samples, several extensions can be
// loaded side by side here -- each one typically provides one bridged device
// (see include/matter_dynamic_extension.h). A successful LoadDone puts the
// extension into the lowest free slot; Unload(slot) removes it again,
// together with every endpoint it added. LoadedExtensions is a bitmap of the
// slots in use.
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
        // Field 0: slot (uint8).
        kUnload = 3,
    };

    enum class AttributeId : chip::AttributeId
    {
        kTransferStatus = 0,
        // 1 is ExtensionValue in the 2_llext/ and 3_llext_lvgl/ samples.
        // bitmap8: bit N is set while slot N holds a loaded extension.
        kLoadedExtensions = 2,
    };

    static constexpr size_t kMaxExtensions = 4;

    // Optional symbols an extension can export: its entry point, and a
    // function called about once per second for as long as it is loaded.
    using ExtensionFn = void (*)(void);
    static constexpr char kRunSymbol[]  = "run";
    static constexpr char kPollSymbol[] = "poll";
    static constexpr chip::System::Clock::Timeout kPollInterval = chip::System::Clock::Seconds16(1);

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
    // well below the LLEXT heap budget (llext.conf's CONFIG_LLEXT_HEAP_SIZE).
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
    chip::app::DataModel::ActionReturnStatus HandleUnload(chip::TLV::TLVReader & input);

    void SetTransferStatus(TransferStatus status);
    void ResetTransfer();

    // One loaded extension. Its address doubles as the owner token for the
    // endpoints it adds (see DynamicEndpointManager).
    struct Slot
    {
        struct llext * ext = nullptr;
        // The buffer llext runs the extension from in place; see
        // ResetTransfer()'s comment.
        uint8_t * buffer = nullptr;
        ExtensionFn poll = nullptr;
    };

    void UnloadExtension(Slot & slot);
    uint8_t LoadedExtensions() const;
    static void OnPollTimer(chip::System::Layer * layer, void * context);

    TransferStatus mTransferStatus = TransferStatus::kIdle;
    uint8_t * mBuffer              = nullptr;
    size_t mExpectedSize           = 0;
    size_t mReceivedBytes          = 0;
    uint32_t mExpectedCrc          = 0;

    Slot mSlots[kMaxExtensions];
    bool mPollTimerRunning = false;
};
