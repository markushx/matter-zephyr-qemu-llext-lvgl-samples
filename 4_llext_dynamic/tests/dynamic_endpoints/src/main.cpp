/*
 * ztest suite for the 4_llext_dynamic sample. Boots the sample's real Matter
 * server, sends the embedded sample extensions through the
 * ExtensionLoaderCluster, and then talks to the endpoints they add the way a
 * controller would -- attribute reads and writes and command invokes through
 * the data model provider -- everything except the Matter transport and a
 * commissioned controller in between.
 */

#include "AppTask.h"
#include "dynamic/DynamicEndpointManager.h"
#include "llext/ExtensionLoaderCluster.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Commands.h>
#include <app/data-model-provider/tests/ReadTesting.h>
#include <app/data-model-provider/tests/WriteTesting.h>
#include <app/data-model/Decode.h>
#include <app/data-model/DecodableList.h>
#include <app/data-model/Nullable.h>
#include <app/server-cluster/testing/MockCommandHandler.h>
#include <clusters/Descriptor/Structs.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <lib/core/TLV.h>
#include <platform/CHIPDeviceLayer.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using Loader          = ExtensionLoaderCluster;
using LoaderCommand   = ExtensionLoaderCluster::CommandId;
using LoaderAttribute = ExtensionLoaderCluster::AttributeId;
using Status          = Protocols::InteractionModel::Status;

namespace {

const uint8_t kOnOffLightExt[] = {
#include <onoff_light.inc>
};
const uint8_t kTempSensorExt[] = {
#include <temp_sensor.inc>
};
const uint8_t kCounterExt[] = {
#include <counter.inc>
};

// What extensions/counter/src/counter.c defines.
constexpr ClusterId kCounterCluster        = 0xFFF1FC02;
constexpr AttributeId kCounterCount        = 0;
constexpr AttributeId kCounterStep         = 1;
constexpr CommandId kCounterIncrement      = 0;
constexpr CommandId kCounterReset          = 1;
constexpr DeviceTypeId kCounterDeviceType  = 0xFFF10001;
constexpr DeviceTypeId kBridgedNodeType    = 0x0013;
constexpr DeviceTypeId kOnOffLightType     = 0x0100;
constexpr EndpointId kAggregatorEndpoint   = DynamicEndpointManager::kAggregatorEndpointId;

// The Matter event loop runs in its own thread once Setup() has started it;
// everything that touches the stack from the test thread holds this.
struct StackLock
{
    StackLock() { DeviceLayer::PlatformMgr().LockChipStack(); }
    ~StackLock() { DeviceLayer::PlatformMgr().UnlockChipStack(); }
};

DataModel::Provider & Provider()
{
    return CodegenDataModelProvider::Instance();
}

Status ToStatus(const DataModel::ActionReturnStatus & status)
{
    return status.GetStatusCode().GetStatus();
}

const auto kNoFields = [](TLV::TLVWriter &) { return CHIP_NO_ERROR; };

// Invokes a command with its fields encoded as the anonymous TLV struct a
// real invoke carries. Returns the status a controller would get back.
template <typename EncodeFields>
Status Invoke(EndpointId endpoint, ClusterId cluster, CommandId command, EncodeFields && encodeFields)
{
    static uint8_t buffer[Loader::kMaxChunkSize + 64];
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

    StackLock lock;
    const DataModel::InvokeRequest request({ endpoint, cluster, command }, Testing::kAdminSubjectDescriptor);
    Testing::MockCommandHandler handler;
    auto result = Provider().InvokeCommand(request, reader, &handler);
    if (result.has_value())
    {
        return ToStatus(*result);
    }
    // Handlers that answer through the CommandHandler instead of by return
    // value (Ember cluster servers, CommandHandlerInterface).
    zassert_true(handler.HasStatus(), "command produced neither a return status nor a response status");
    return handler.GetLastStatus().status.GetStatus();
}

Status Invoke(EndpointId endpoint, ClusterId cluster, CommandId command)
{
    return Invoke(endpoint, cluster, command, kNoFields);
}

template <typename T>
Status Read(EndpointId endpoint, ClusterId cluster, AttributeId attribute, T & value)
{
    StackLock lock;
    Testing::ReadOperation read(endpoint, cluster, attribute);
    read.SetSubjectDescriptor(Testing::kAdminSubjectDescriptor);
    auto encoder = read.StartEncoding();
    auto status  = Provider().ReadAttribute(read.GetRequest(), *encoder);
    if (!status.IsSuccess())
    {
        return ToStatus(status);
    }
    zassert_equal(read.FinishEncoding(), CHIP_NO_ERROR);

    std::vector<Testing::DecodedAttributeData> items;
    zassert_equal(read.GetEncodedIBs().Decode(items), CHIP_NO_ERROR);
    zassert_equal(items.size(), 1U);
    zassert_equal(DataModel::Decode(items[0].dataReader, value), CHIP_NO_ERROR);
    return Status::Success;
}

// For when the read is expected to work.
template <typename T>
T Get(EndpointId endpoint, ClusterId cluster, AttributeId attribute)
{
    T value{};
    Status status = Read(endpoint, cluster, attribute, value);
    zassert_equal(status, Status::Success, "read of %u/0x%lx/0x%lx failed: 0x%02x", endpoint, static_cast<unsigned long>(cluster),
                  static_cast<unsigned long>(attribute), to_underlying(status));
    return value;
}

// Lists and strings decode to views into the read's buffer, so they are
// copied out while it is still alive.
template <typename T, typename Convert>
auto GetList(EndpointId endpoint, ClusterId cluster, AttributeId attribute, Convert && convert)
{
    std::vector<decltype(convert(std::declval<const T &>()))> result;

    StackLock lock;
    Testing::ReadOperation read(endpoint, cluster, attribute);
    read.SetSubjectDescriptor(Testing::kAdminSubjectDescriptor);
    auto encoder = read.StartEncoding();
    zassert_true(Provider().ReadAttribute(read.GetRequest(), *encoder).IsSuccess());
    zassert_equal(read.FinishEncoding(), CHIP_NO_ERROR);

    std::vector<Testing::DecodedAttributeData> items;
    zassert_equal(read.GetEncodedIBs().Decode(items), CHIP_NO_ERROR);
    zassert_equal(items.size(), 1U);

    DataModel::DecodableList<T> list;
    zassert_equal(DataModel::Decode(items[0].dataReader, list), CHIP_NO_ERROR);
    auto it = list.begin();
    while (it.Next())
    {
        result.push_back(convert(it.GetValue()));
    }
    zassert_equal(it.GetStatus(), CHIP_NO_ERROR);
    return result;
}

std::vector<EndpointId> PartsList(EndpointId endpoint)
{
    return GetList<EndpointId>(endpoint, Descriptor::Id, Descriptor::Attributes::PartsList::Id,
                               [](const EndpointId & id) { return id; });
}

std::vector<ClusterId> ServerList(EndpointId endpoint)
{
    return GetList<ClusterId>(endpoint, Descriptor::Id, Descriptor::Attributes::ServerList::Id,
                              [](const ClusterId & id) { return id; });
}

std::vector<DeviceTypeId> DeviceTypes(EndpointId endpoint)
{
    return GetList<Descriptor::Structs::DeviceTypeStruct::DecodableType>(
        endpoint, Descriptor::Id, Descriptor::Attributes::DeviceTypeList::Id,
        [](const Descriptor::Structs::DeviceTypeStruct::DecodableType & type) { return type.deviceType; });
}

std::string NodeLabel(EndpointId endpoint)
{
    StackLock lock;
    Testing::ReadOperation read(endpoint, BridgedDeviceBasicInformation::Id,
                                BridgedDeviceBasicInformation::Attributes::NodeLabel::Id);
    read.SetSubjectDescriptor(Testing::kAdminSubjectDescriptor);
    auto encoder = read.StartEncoding();
    zassert_true(Provider().ReadAttribute(read.GetRequest(), *encoder).IsSuccess());
    zassert_equal(read.FinishEncoding(), CHIP_NO_ERROR);

    std::vector<Testing::DecodedAttributeData> items;
    zassert_equal(read.GetEncodedIBs().Decode(items), CHIP_NO_ERROR);
    zassert_equal(items.size(), 1U);

    CharSpan label;
    zassert_equal(DataModel::Decode(items[0].dataReader, label), CHIP_NO_ERROR);
    return std::string(label.data(), label.size());
}

template <typename T>
Status Write(EndpointId endpoint, ClusterId cluster, AttributeId attribute, const T & value)
{
    StackLock lock;
    Testing::WriteOperation write(endpoint, cluster, attribute);
    write.SetSubjectDescriptor(Testing::kAdminSubjectDescriptor);
    AttributeValueDecoder decoder = write.DecoderFor<T>(value);
    return ToStatus(Provider().WriteAttribute(write.GetRequest(), decoder));
}

template <typename T>
bool Contains(const std::vector<T> & list, T value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

// ---- ExtensionLoaderCluster ----

Status LoaderInvoke(LoaderCommand command)
{
    return Invoke(Loader::kEndpointId, Loader::kClusterId, to_underlying(command));
}

// The whole transfer scripts/send-llext.py does, chunked the same way.
// Returns LoadDone's status; everything before it is asserted to succeed.
Status Send(ByteSpan ext)
{
    const uint32_t size = static_cast<uint32_t>(ext.size());
    const uint32_t crc  = crc32_ieee(ext.data(), ext.size());

    zassert_equal(Invoke(Loader::kEndpointId, Loader::kClusterId, to_underlying(LoaderCommand::kLoadStart),
                         [&](TLV::TLVWriter & writer) {
                             ReturnErrorOnFailure(writer.Put(TLV::ContextTag(0), size));
                             return writer.Put(TLV::ContextTag(1), crc);
                         }),
                  Status::Success);
    for (size_t offset = 0; offset < ext.size(); offset += Loader::kMaxChunkSize)
    {
        ByteSpan chunk = ext.SubSpan(offset, std::min(Loader::kMaxChunkSize, ext.size() - offset));
        zassert_equal(Invoke(Loader::kEndpointId, Loader::kClusterId, to_underlying(LoaderCommand::kLoadChunk),
                             [&](TLV::TLVWriter & writer) {
                                 ReturnErrorOnFailure(writer.Put(TLV::ContextTag(0), static_cast<uint32_t>(offset)));
                                 return writer.Put(TLV::ContextTag(1), chunk);
                             }),
                      Status::Success);
    }
    return LoaderInvoke(LoaderCommand::kLoadDone);
}

Status Unload(uint8_t slot)
{
    return Invoke(Loader::kEndpointId, Loader::kClusterId, to_underlying(LoaderCommand::kUnload),
                  [&](TLV::TLVWriter & writer) { return writer.Put(TLV::ContextTag(0), slot); });
}

uint8_t LoadedExtensions()
{
    return Get<uint8_t>(Loader::kEndpointId, Loader::kClusterId, to_underlying(LoaderAttribute::kLoadedExtensions));
}

// Loads an extension that is expected to add exactly one endpoint, and
// returns that endpoint's ID.
EndpointId LoadDevice(ByteSpan ext)
{
    std::vector<EndpointId> before = PartsList(kAggregatorEndpoint);
    zassert_equal(Send(ext), Status::Success);
    std::vector<EndpointId> after = PartsList(kAggregatorEndpoint);
    zassert_equal(after.size(), before.size() + 1, "extension did not add exactly one endpoint");

    for (EndpointId endpoint : after)
    {
        if (!Contains(before, endpoint))
        {
            return endpoint;
        }
    }
    zassert_unreachable();
    return kInvalidEndpointId;
}

void * Setup()
{
    zassert_equal(AppTask::Instance().Init(), CHIP_NO_ERROR);
    zassert_equal(DeviceLayer::PlatformMgr().StartEventLoopTask(), CHIP_NO_ERROR);
    return nullptr;
}

// Every test starts from, and has to leave behind, an empty bridge.
void After(void *)
{
    for (uint8_t slot = 0; slot < Loader::kMaxExtensions; slot++)
    {
        Unload(slot);
    }
    zassert_equal(LoadedExtensions(), 0);
    zassert_equal(PartsList(kAggregatorEndpoint).size(), 0U);

    StackLock lock;
    zassert_equal(DynamicEndpointManager::Count(), 0U);
}

} // namespace

ZTEST_SUITE(dynamic_endpoints, nullptr, Setup, nullptr, After, nullptr);

ZTEST(dynamic_endpoints, test_empty_bridge)
{
    zassert_equal(LoadedExtensions(), 0);
    // In particular: the data model's placeholder endpoint is hidden.
    zassert_equal(PartsList(kAggregatorEndpoint).size(), 0U);
    zassert_equal(Unload(0), Status::NotFound);
    zassert_equal(Unload(Loader::kMaxExtensions), Status::ConstraintError);
}

// A standard cluster whose commands the app's own server handles: the
// extension only stores the attribute.
ZTEST(dynamic_endpoints, test_onoff_light)
{
    EndpointId light = LoadDevice(ByteSpan(kOnOffLightExt));
    zassert_equal(LoadedExtensions(), 0x01);

    // It is a bridged node below the Aggregator...
    auto deviceTypes = DeviceTypes(light);
    zassert_true(Contains(deviceTypes, kOnOffLightType));
    zassert_true(Contains(deviceTypes, kBridgedNodeType));
    zassert_true(Contains(PartsList(0), light), "new endpoint missing from the root node's PartsList");
    zassert_equal(NodeLabel(light), std::string("LLEXT light"));

    // ...with the extension's cluster plus the two the app adds.
    auto servers = ServerList(light);
    zassert_true(Contains(servers, static_cast<ClusterId>(OnOff::Id)));
    zassert_true(Contains(servers, static_cast<ClusterId>(Descriptor::Id)));
    zassert_true(Contains(servers, static_cast<ClusterId>(BridgedDeviceBasicInformation::Id)));
    zassert_equal(servers.size(), 3U);

    // Globals are served by the app from the extension's description.
    zassert_equal(Get<uint16_t>(light, OnOff::Id, Globals::Attributes::ClusterRevision::Id), 6);
    zassert_equal(Get<uint32_t>(light, OnOff::Id, Globals::Attributes::FeatureMap::Id), 0U);

    zassert_false(Get<bool>(light, OnOff::Id, OnOff::Attributes::OnOff::Id));
    zassert_equal(Invoke(light, OnOff::Id, OnOff::Commands::On::Id), Status::Success);
    zassert_true(Get<bool>(light, OnOff::Id, OnOff::Attributes::OnOff::Id));
    zassert_equal(Invoke(light, OnOff::Id, OnOff::Commands::Toggle::Id), Status::Success);
    zassert_false(Get<bool>(light, OnOff::Id, OnOff::Attributes::OnOff::Id));
    zassert_equal(Invoke(light, OnOff::Id, OnOff::Commands::Toggle::Id), Status::Success);
    zassert_true(Get<bool>(light, OnOff::Id, OnOff::Attributes::OnOff::Id));
    zassert_equal(Invoke(light, OnOff::Id, OnOff::Commands::Off::Id), Status::Success);
    zassert_false(Get<bool>(light, OnOff::Id, OnOff::Attributes::OnOff::Id));

    // Not declared writable by the extension.
    zassert_not_equal(Write(light, OnOff::Id, OnOff::Attributes::OnOff::Id, true), Status::Success);
}

// A value the extension changes by itself, from its poll() function.
ZTEST(dynamic_endpoints, test_temperature_sensor)
{
    using namespace TemperatureMeasurement::Attributes;
    using Temperature = DataModel::Nullable<int16_t>;

    EndpointId sensor = LoadDevice(ByteSpan(kTempSensorExt));
    zassert_equal(NodeLabel(sensor), std::string("LLEXT thermometer"));

    Temperature min = Get<Temperature>(sensor, TemperatureMeasurement::Id, MinMeasuredValue::Id);
    Temperature max = Get<Temperature>(sensor, TemperatureMeasurement::Id, MaxMeasuredValue::Id);
    zassert_false(min.IsNull() || max.IsNull());
    zassert_equal(min.Value(), 2000);
    zassert_equal(max.Value(), 2500);

    Temperature first = Get<Temperature>(sensor, TemperatureMeasurement::Id, MeasuredValue::Id);
    zassert_false(first.IsNull());
    zassert_between_inclusive(first.Value(), min.Value(), max.Value());

    // poll() runs about once per second and moves the value every time.
    k_sleep(K_MSEC(2500));
    Temperature second = Get<Temperature>(sensor, TemperatureMeasurement::Id, MeasuredValue::Id);
    zassert_false(second.IsNull());
    zassert_between_inclusive(second.Value(), min.Value(), max.Value());
    zassert_not_equal(first.Value(), second.Value(), "MeasuredValue did not change: poll() is not being called");

    // No commands, nothing writable.
    zassert_not_equal(Write(sensor, TemperatureMeasurement::Id, MeasuredValue::Id, Temperature(int16_t(1))), Status::Success);
}

// A cluster the app knows nothing about: attributes, a writable attribute
// with its own constraint, and commands implemented by the extension.
ZTEST(dynamic_endpoints, test_custom_cluster_with_commands)
{
    EndpointId counter = LoadDevice(ByteSpan(kCounterExt));

    zassert_true(Contains(DeviceTypes(counter), kCounterDeviceType));
    zassert_true(Contains(ServerList(counter), kCounterCluster));
    zassert_equal(Get<uint16_t>(counter, kCounterCluster, Globals::Attributes::ClusterRevision::Id), 1);

    zassert_equal(Get<uint32_t>(counter, kCounterCluster, kCounterCount), 0U);
    zassert_equal(Get<uint8_t>(counter, kCounterCluster, kCounterStep), 1);

    zassert_equal(Invoke(counter, kCounterCluster, kCounterIncrement), Status::Success);
    zassert_equal(Get<uint32_t>(counter, kCounterCluster, kCounterCount), 1U);

    // The write reaches the extension, and so does its verdict on the value.
    zassert_equal(Write(counter, kCounterCluster, kCounterStep, static_cast<uint8_t>(5)), Status::Success);
    zassert_equal(Get<uint8_t>(counter, kCounterCluster, kCounterStep), 5);
    zassert_equal(Write(counter, kCounterCluster, kCounterStep, static_cast<uint8_t>(0)), Status::ConstraintError);
    zassert_equal(Get<uint8_t>(counter, kCounterCluster, kCounterStep), 5);
    zassert_not_equal(Write(counter, kCounterCluster, kCounterCount, static_cast<uint32_t>(7)), Status::Success);

    zassert_equal(Invoke(counter, kCounterCluster, kCounterIncrement), Status::Success);
    zassert_equal(Get<uint32_t>(counter, kCounterCluster, kCounterCount), 6U);

    // Command fields arrive in the extension.
    auto incrementBy = [&](uint32_t by) {
        return Invoke(counter, kCounterCluster, kCounterIncrement,
                      [&](TLV::TLVWriter & writer) { return writer.Put(TLV::ContextTag(0), by); });
    };
    zassert_equal(incrementBy(10), Status::Success);
    zassert_equal(Get<uint32_t>(counter, kCounterCluster, kCounterCount), 16U);
    // ...and the status code it answers with reaches the controller.
    zassert_equal(incrementBy(5000), Status::ConstraintError);
    zassert_equal(Get<uint32_t>(counter, kCounterCluster, kCounterCount), 16U);

    zassert_equal(Invoke(counter, kCounterCluster, kCounterReset), Status::Success);
    zassert_equal(Get<uint32_t>(counter, kCounterCluster, kCounterCount), 0U);
}

// The bridge part: several extensions side by side, removed individually.
ZTEST(dynamic_endpoints, test_several_extensions_and_unload)
{
    EndpointId light   = LoadDevice(ByteSpan(kOnOffLightExt));  // slot 0
    EndpointId sensor  = LoadDevice(ByteSpan(kTempSensorExt));  // slot 1
    EndpointId counter = LoadDevice(ByteSpan(kCounterExt));     // slot 2
    zassert_equal(LoadedExtensions(), 0x07);
    zassert_true(light != sensor && sensor != counter && light != counter);
    zassert_equal(PartsList(kAggregatorEndpoint).size(), 3U);

    zassert_equal(Invoke(light, OnOff::Id, OnOff::Commands::On::Id), Status::Success);
    zassert_equal(Invoke(counter, kCounterCluster, kCounterIncrement), Status::Success);

    // Unloading one takes exactly its endpoint away.
    zassert_equal(Unload(1), Status::Success);
    zassert_equal(LoadedExtensions(), 0x05);
    auto parts = PartsList(kAggregatorEndpoint);
    zassert_equal(parts.size(), 2U);
    zassert_false(Contains(parts, sensor));
    DataModel::Nullable<int16_t> gone;
    zassert_not_equal(
        Read(sensor, TemperatureMeasurement::Id, TemperatureMeasurement::Attributes::MeasuredValue::Id, gone), Status::Success);
    zassert_equal(Unload(1), Status::NotFound);

    // The others are untouched, state included.
    zassert_true(Get<bool>(light, OnOff::Id, OnOff::Attributes::OnOff::Id));
    zassert_equal(Get<uint32_t>(counter, kCounterCluster, kCounterCount), 1U);

    // The freed slot is reused, and the same extension can be loaded twice:
    // two independent lights.
    EndpointId light2 = LoadDevice(ByteSpan(kOnOffLightExt)); // slot 1
    zassert_equal(LoadedExtensions(), 0x07);
    zassert_not_equal(light2, light);
    zassert_not_equal(light2, sensor, "endpoint ID of a removed endpoint was reused right away");
    zassert_false(Get<bool>(light2, OnOff::Id, OnOff::Attributes::OnOff::Id));
    zassert_true(Get<bool>(light, OnOff::Id, OnOff::Attributes::OnOff::Id));

    // All slots full: the next load is refused and changes nothing.
    LoadDevice(ByteSpan(kTempSensorExt)); // slot 3
    zassert_equal(LoadedExtensions(), 0x0F);
    zassert_equal(Send(ByteSpan(kCounterExt)), Status::ResourceExhausted);
    zassert_equal(LoadedExtensions(), 0x0F);
    zassert_equal(PartsList(kAggregatorEndpoint).size(), 4U);
}
