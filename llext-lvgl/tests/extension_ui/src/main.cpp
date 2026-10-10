/*
 * ztest suite for UI extensions. Sends the embedded sample UI extensions
 * through the ExtensionLoaderCluster's real LoadStart/LoadChunk/LoadDone
 * command handlers, with the real GUI thread rendering to the display --
 * everything except the Matter transport in between.
 *
 * What ends up on screen can't be asserted from in here, so after each step
 * the test prints a "SCREENSHOT <name>" line and pauses;
 * scripts/run-llext-ztest.sh takes a QEMU screendump for each and fails the
 * run unless all of them differ from each other. Every step below therefore
 * has to produce a picture not seen before.
 */

#include "gui/BulbUi.h"
#include "llext/ExtensionLoaderCluster.h"

#include <app/data-model-provider/tests/ReadTesting.h>
#include <app/data-model/Decode.h>
#include <lib/core/TLV.h>
#include <lib/support/CHIPMem.h>

#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <vector>

using namespace chip;
using namespace chip::app;
using Cluster   = ExtensionLoaderCluster;
using Command   = ExtensionLoaderCluster::CommandId;
using Attribute = ExtensionLoaderCluster::AttributeId;

namespace {

const uint8_t kUiBarExt[] = {
#include <ui_bar.inc>
};
const uint8_t kUiLedExt[] = {
#include <ui_led.inc>
};

// Encodes a command's fields as the anonymous TLV struct a real invoke
// carries and hands it to the cluster.
template <typename EncodeFields>
DataModel::ActionReturnStatus Invoke(Cluster & cluster, Command command, EncodeFields && encodeFields)
{
    uint8_t buffer[Cluster::kMaxChunkSize + 64];
    TLV::TLVWriter writer;
    writer.Init(buffer);

    TLV::TLVType outer;
    VerifyOrDie(writer.StartContainer(TLV::AnonymousTag(), TLV::kTLVType_Structure, outer) == CHIP_NO_ERROR);
    VerifyOrDie(encodeFields(writer) == CHIP_NO_ERROR);
    VerifyOrDie(writer.EndContainer(outer) == CHIP_NO_ERROR);
    VerifyOrDie(writer.Finalize() == CHIP_NO_ERROR);

    TLV::TLVReader reader;
    reader.Init(buffer, writer.GetLengthWritten());
    VerifyOrDie(reader.Next(TLV::kTLVType_Structure, TLV::AnonymousTag()) == CHIP_NO_ERROR);

    const DataModel::InvokeRequest request({ Cluster::kEndpointId, Cluster::kClusterId, to_underlying(command) },
                                           Testing::kAdminSubjectDescriptor);
    auto status = cluster.InvokeCommand(request, reader, nullptr);
    VerifyOrDie(status.has_value());
    return *status;
}

// The whole transfer scripts/send-llext.py does, chunked the same way.
void Load(Cluster & cluster, ByteSpan ext)
{
    zassert_true(Invoke(cluster, Command::kLoadStart, [&](TLV::TLVWriter & writer) {
                     ReturnErrorOnFailure(writer.Put(TLV::ContextTag(0), static_cast<uint32_t>(ext.size())));
                     return writer.Put(TLV::ContextTag(1), crc32_ieee(ext.data(), ext.size()));
                 }).IsSuccess());

    for (size_t offset = 0; offset < ext.size(); offset += Cluster::kMaxChunkSize)
    {
        ByteSpan chunk = ext.SubSpan(offset, std::min(Cluster::kMaxChunkSize, ext.size() - offset));
        zassert_true(Invoke(cluster, Command::kLoadChunk, [&](TLV::TLVWriter & writer) {
                         ReturnErrorOnFailure(writer.Put(TLV::ContextTag(0), static_cast<uint32_t>(offset)));
                         return writer.Put(TLV::ContextTag(1), chunk);
                     }).IsSuccess());
    }

    zassert_true(Invoke(cluster, Command::kLoadDone, [](TLV::TLVWriter &) { return CHIP_NO_ERROR; }).IsSuccess(),
                 "LoadDone failed -- an \"Undefined symbol\" above means src/gui/LvglExports.c lacks an entry");
}

Cluster::TransferStatus ReadTransferStatus(Cluster & cluster)
{
    Testing::ReadOperation read(Cluster::kEndpointId, Cluster::kClusterId, to_underlying(Attribute::kTransferStatus));
    auto encoder = read.StartEncoding();
    zassert_true(cluster.ReadAttribute(read.GetRequest(), *encoder).IsSuccess());
    zassert_equal(read.FinishEncoding(), CHIP_NO_ERROR);

    std::vector<Testing::DecodedAttributeData> items;
    zassert_equal(read.GetEncodedIBs().Decode(items), CHIP_NO_ERROR);
    zassert_equal(items.size(), 1U);

    uint8_t value = 0;
    zassert_equal(DataModel::Decode(items[0].dataReader, value), CHIP_NO_ERROR);
    return static_cast<Cluster::TransferStatus>(value);
}

// Lets the GUI thread render the current state, then asks the runner for a
// screendump and gives it time to take one.
void Screenshot(const char * name)
{
    k_msleep(300);
    printk("SCREENSHOT %s\n", name);
    k_msleep(2000);
}

void * Setup()
{
    zassert_equal(Platform::MemoryInit(), CHIP_NO_ERROR);
    BulbUi::Init();
    k_msleep(500);
    return nullptr;
}

} // namespace

ZTEST_SUITE(extension_ui, nullptr, Setup, nullptr, nullptr, nullptr);

ZTEST(extension_ui, test_extensions_replace_ui)
{
    BulbUi::SetState(false, 254);
    Screenshot("1-builtin-off");

    {
        Cluster cluster;

        Load(cluster, ByteSpan(kUiBarExt));
        zassert_equal(ReadTransferStatus(cluster), Cluster::TransferStatus::kLoaded);
        Screenshot("2-bar-off");

        // The extension's UI follows the bulb state...
        BulbUi::SetState(true, 127);
        Screenshot("3-bar-on-50");

        // ...and a second extension replaces the first, picking up the
        // current state rather than starting blank.
        Load(cluster, ByteSpan(kUiLedExt));
        zassert_equal(ReadTransferStatus(cluster), Cluster::TransferStatus::kLoaded);
        Screenshot("4-led-on-50");

        BulbUi::SetState(true, 254);
        Screenshot("5-led-on-100");

        // And back again, to prove an unloaded UI can be reloaded.
        Load(cluster, ByteSpan(kUiBarExt));
        Screenshot("6-bar-on-100");
    }

    // The cluster going away unloads its extension, which must bring the
    // built-in UI back rather than leave the screen to code that's gone.
    Screenshot("7-builtin-on-100");
}
