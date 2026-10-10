/*
 * ztest suite for ExtensionLoaderCluster. Sends the embedded sample
 * extensions through the cluster's real LoadStart/LoadChunk/LoadDone command
 * handlers and reads its attributes back through the real ReadAttribute
 * path -- everything except the Matter transport in between.
 */

#include "llext/ExtensionLoaderCluster.h"

#include <app/data-model-provider/tests/ReadTesting.h>
#include <app/data-model/Decode.h>
#include <app/data-model/Nullable.h>
#include <lib/core/TLV.h>
#include <lib/support/CHIPMem.h>

#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <vector>

using namespace chip;
using namespace chip::app;
using Cluster   = ExtensionLoaderCluster;
using Command   = ExtensionLoaderCluster::CommandId;
using Attribute = ExtensionLoaderCluster::AttributeId;

namespace {

const uint8_t kHelloExt[] = {
#include <hello.inc>
};
const uint8_t kValue42Ext[] = {
#include <value42.inc>
};
const uint8_t kValue1337Ext[] = {
#include <value1337.inc>
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

DataModel::ActionReturnStatus LoadStart(Cluster & cluster, uint32_t size, uint32_t crc)
{
    return Invoke(cluster, Command::kLoadStart, [&](TLV::TLVWriter & writer) {
        ReturnErrorOnFailure(writer.Put(TLV::ContextTag(0), size));
        return writer.Put(TLV::ContextTag(1), crc);
    });
}

DataModel::ActionReturnStatus LoadChunk(Cluster & cluster, uint32_t offset, ByteSpan data)
{
    return Invoke(cluster, Command::kLoadChunk, [&](TLV::TLVWriter & writer) {
        ReturnErrorOnFailure(writer.Put(TLV::ContextTag(0), offset));
        return writer.Put(TLV::ContextTag(1), data);
    });
}

DataModel::ActionReturnStatus LoadDone(Cluster & cluster)
{
    return Invoke(cluster, Command::kLoadDone, [](TLV::TLVWriter &) { return CHIP_NO_ERROR; });
}

// The whole transfer scripts/send-llext.py does, chunked the same way.
// Returns LoadDone's status; everything before it is asserted to succeed.
DataModel::ActionReturnStatus Send(Cluster & cluster, ByteSpan ext, uint32_t crc)
{
    zassert_true(LoadStart(cluster, static_cast<uint32_t>(ext.size()), crc).IsSuccess());
    for (size_t offset = 0; offset < ext.size(); offset += Cluster::kMaxChunkSize)
    {
        size_t len = std::min(Cluster::kMaxChunkSize, ext.size() - offset);
        zassert_true(LoadChunk(cluster, static_cast<uint32_t>(offset), ext.SubSpan(offset, len)).IsSuccess());
    }
    return LoadDone(cluster);
}

void Load(Cluster & cluster, ByteSpan ext)
{
    zassert_true(Send(cluster, ext, crc32_ieee(ext.data(), ext.size())).IsSuccess());
}

template <typename T>
T Read(Cluster & cluster, Attribute attribute)
{
    Testing::ReadOperation read(Cluster::kEndpointId, Cluster::kClusterId, to_underlying(attribute));
    auto encoder = read.StartEncoding();
    zassert_true(cluster.ReadAttribute(read.GetRequest(), *encoder).IsSuccess());
    zassert_equal(read.FinishEncoding(), CHIP_NO_ERROR);

    std::vector<Testing::DecodedAttributeData> items;
    zassert_equal(read.GetEncodedIBs().Decode(items), CHIP_NO_ERROR);
    zassert_equal(items.size(), 1U);

    T value{};
    zassert_equal(DataModel::Decode(items[0].dataReader, value), CHIP_NO_ERROR);
    return value;
}

Cluster::TransferStatus ReadTransferStatus(Cluster & cluster)
{
    return static_cast<Cluster::TransferStatus>(Read<uint8_t>(cluster, Attribute::kTransferStatus));
}

DataModel::Nullable<int32_t> ReadValue(Cluster & cluster)
{
    return Read<DataModel::Nullable<int32_t>>(cluster, Attribute::kExtensionValue);
}

void ExpectValue(Cluster & cluster, int32_t expected)
{
    auto value = ReadValue(cluster);
    zassert_false(value.IsNull(), "ExtensionValue is null, expected %d", expected);
    zassert_equal(value.Value(), expected, "ExtensionValue is %d, expected %d", value.Value(), expected);
}

void * Setup()
{
    zassert_equal(Platform::MemoryInit(), CHIP_NO_ERROR);
    return nullptr;
}

} // namespace

ZTEST_SUITE(extension_value, nullptr, Setup, nullptr, nullptr, nullptr);

ZTEST(extension_value, test_null_without_extension)
{
    Cluster cluster;

    zassert_equal(ReadTransferStatus(cluster), Cluster::TransferStatus::kIdle);
    zassert_true(ReadValue(cluster).IsNull());
}

// The point of the attribute: two different extensions, two different values.
ZTEST(extension_value, test_two_extensions_two_values)
{
    Cluster cluster;

    Load(cluster, ByteSpan(kValue42Ext));
    zassert_equal(ReadTransferStatus(cluster), Cluster::TransferStatus::kLoaded);
    ExpectValue(cluster, 42);

    Load(cluster, ByteSpan(kValue1337Ext));
    zassert_equal(ReadTransferStatus(cluster), Cluster::TransferStatus::kLoaded);
    ExpectValue(cluster, 1337);

    Load(cluster, ByteSpan(kValue42Ext));
    ExpectValue(cluster, 42);
}

ZTEST(extension_value, test_null_for_extension_without_get_value)
{
    Cluster cluster;

    Load(cluster, ByteSpan(kValue42Ext));
    ExpectValue(cluster, 42);

    Load(cluster, ByteSpan(kHelloExt));
    zassert_equal(ReadTransferStatus(cluster), Cluster::TransferStatus::kLoaded);
    zassert_true(ReadValue(cluster).IsNull());
}

ZTEST(extension_value, test_failed_transfer_keeps_previous_extension)
{
    Cluster cluster;

    Load(cluster, ByteSpan(kValue42Ext));

    ByteSpan ext(kValue1337Ext);
    zassert_false(Send(cluster, ext, crc32_ieee(ext.data(), ext.size()) ^ 1).IsSuccess());
    zassert_equal(ReadTransferStatus(cluster), Cluster::TransferStatus::kError);
    ExpectValue(cluster, 42);

    Load(cluster, ext);
    ExpectValue(cluster, 1337);
}
