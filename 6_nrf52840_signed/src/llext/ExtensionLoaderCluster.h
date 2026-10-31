#pragma once

#include "ExtensionSignature.h"
#include "ExtensionStore.h"

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
// A loaded extension survives a reboot, see ExtensionStore and
// RestoreExtensions(). That makes installing one a lasting change to what runs
// on the device, with full privileges, so the commands need Administer and the
// extension has to be signed, see ExtensionSignature.
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
        // bitmap8: bit N is set if slot N has a stored extension that wasn't
        // restored at boot, see RestoreExtensions(). Unload(N) or sending
        // another extension clears it.
        kDisabledExtensions = 3,
    };

    // Bounded by the RAM an nRF52840 has left next to the Matter stack, as is
    // kMaxExtensionSize below (CONFIG_HEAP_MEM_POOL_SIZE).
    static constexpr size_t kMaxExtensions = ExtensionStore::kSlotCount;

    // A stored extension is given up on after this many restores in a row
    // that ended in a reset before the device had been up for
    // kRestoreStablePeriod. This counts for all slots: it isn't known which
    // extension caused the reset. A restore that is just unlucky (a power cut)
    // doesn't disable anything, as it takes this many.
    static constexpr uint8_t kMaxRestoreAttempts = 3;
    static constexpr chip::System::Clock::Timeout kRestoreStablePeriod = chip::System::Clock::Seconds16(60);

    // Optional symbols an extension can export: its entry point, and a
    // function called about once per second for as long as it is loaded.
    using ExtensionFn = void (*)(void);
    static constexpr char kRunSymbol[]  = "run";
    static constexpr char kPollSymbol[] = "poll";
    static constexpr chip::System::Clock::Timeout kPollInterval = chip::System::Clock::Seconds16(1);

    // Why LoadDone refused an extension, as cluster-specific status codes:
    // the firmware usually has no logging to tell.
    enum class ClusterStatus : uint8_t
    {
        kInvalidSignature = 1,
        // Signed, but for another build: sign it again with this build's zephyr.bin.
        kWrongFirmware = 2,
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

    // Largest extension binary we'll buffer in RAM before loading, including
    // what signing adds to it.
    static constexpr size_t kMaxExtensionSize = ExtensionStore::kSlotSize;

    ExtensionLoaderCluster() : DefaultServerCluster({ kEndpointId, kClusterId }) {}
    ~ExtensionLoaderCluster() override;

    // Loads the extensions stored by earlier LoadDone commands, unless their
    // signature doesn't check out for this firmware or they keep resetting the
    // device (see kMaxRestoreAttempts); those end up in kDisabledExtensions
    // instead. Their state in RAM, an On/Off value for example, starts over. Call once, on the
    // Matter thread, once the watchdog runs.
    void RestoreExtensions();

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
        uint32_t uniqueIdSeed = 0;
    };

    // Runs the verified image in `buffer`, which modifies it and which then
    // belongs to the slot. `restoredEndpoints` are the IDs its endpoints had before.
    int LoadIntoSlot(size_t slotIndex, uint8_t * buffer, const ExtensionSignature::Elf & elf, uint32_t uniqueIdSeed,
                     chip::Span<const chip::EndpointId> restoredEndpoints);
    bool RestoreSlot(size_t slotIndex, ExtensionStore::Record & record, uint32_t firmwareId);
    void SetDisabled(size_t slotIndex, bool disabled);
    void UnloadExtension(Slot & slot);
    uint8_t LoadedExtensions() const;
    static void OnPollTimer(chip::System::Layer * layer, void * context);
    static void OnRestoreStable(chip::System::Layer * layer, void * context);

    TransferStatus mTransferStatus = TransferStatus::kIdle;
    uint8_t mDisabledExtensions    = 0;
    uint8_t * mBuffer              = nullptr;
    size_t mExpectedSize           = 0;
    size_t mReceivedBytes          = 0;
    uint32_t mExpectedCrc          = 0;

    Slot mSlots[kMaxExtensions];
    bool mPollTimerRunning = false;
};
