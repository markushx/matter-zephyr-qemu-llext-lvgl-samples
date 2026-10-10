#pragma once

#include <app/ConcreteClusterPath.h>
#include <app/data-model-provider/ActionReturnStatus.h>
#include <app/server-cluster/DefaultServerCluster.h>
#include <lib/core/CHIPError.h>
#include <lib/core/TLVReader.h>

struct llext;

// Manufacturer-specific cluster: transports a Zephyr LLEXT binary to the
// device in chunks, then loads and runs it. See plan/plan-detailed.md,
// Milestone 3.
//
// At most one extension is loaded at a time: a successful LoadDone unloads
// the previous one first. If the loaded extension exports
// `int32_t get_value(void)`, the ExtensionValue attribute reads back whatever
// that function returns (null otherwise) -- i.e. the attribute's value is
// provided by the extension, not by the app.
//
// If it exports `ui_create` and `ui_update` (see
// include/bulb_ui_extension.h), it also replaces the bulb's LVGL UI for as
// long as it stays loaded.
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
        // nullable int32: result of the loaded extension's get_value(), null
        // if nothing is loaded or the extension doesn't export one.
        kExtensionValue = 1,
    };

    // Optional symbol an extension can export to back kExtensionValue.
    using GetValueFn = int32_t (*)(void);
    static constexpr char kGetValueSymbol[] = "get_value";

    // Optional symbol pair an extension can export to provide the bulb UI.
    static constexpr char kUiCreateSymbol[] = "ui_create";
    static constexpr char kUiUpdateSymbol[] = "ui_update";

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

    void SetTransferStatus(TransferStatus status);
    void ResetTransfer();
    void UnloadExtension();

    TransferStatus mTransferStatus = TransferStatus::kIdle;
    uint8_t * mBuffer              = nullptr;
    size_t mExpectedSize           = 0;
    size_t mReceivedBytes          = 0;
    uint32_t mExpectedCrc          = 0;

    // Currently loaded extension, the buffer it lives in (see
    // ResetTransfer()'s comment), and its get_value() if it exports one.
    struct llext * mExt  = nullptr;
    uint8_t * mExtBuffer = nullptr;
    GetValueFn mGetValue = nullptr;
    bool mProvidesUi     = false;
};
