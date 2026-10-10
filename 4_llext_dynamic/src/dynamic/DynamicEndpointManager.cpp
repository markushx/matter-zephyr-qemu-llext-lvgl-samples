#include "DynamicEndpointManager.h"

#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/CommandHandler.h>
#include <app/CommandHandlerInterface.h>
#include <app/CommandHandlerInterfaceRegistry.h>
#include <platform/DefaultTimerDelegate.h>
#include <app/clusters/bridged-device-basic-information-server/BridgedDeviceBasicInformationCluster.h>
#include <app/reporting/reporting.h>
#include <app/server-cluster/ServerClusterInterfaceRegistry.h>
#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <app/util/endpoint-config-defines.h>
#include <crypto/RandUtils.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <lib/core/TLV.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceConfig.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using Status = chip::Protocols::InteractionModel::Status;

namespace {

constexpr DeviceTypeId kBridgedNodeDeviceType = 0x0013;
constexpr uint8_t kBridgedNodeDeviceRevision  = 1;
// Same sizes bridge-app declares its dynamic Descriptor attributes with; the
// values themselves come from the Descriptor cluster server, not from us.
constexpr uint16_t kDescriptorAttributeArraySize = 254;
constexpr size_t kMaxNameLength                  = 32;

struct TypeInfo
{
    EmberAfAttributeType zclType;
    uint16_t size;
};

bool GetTypeInfo(uint8_t type, TypeInfo & info)
{
    switch (type)
    {
    case MATTER_DYN_BOOL:
        info = { ZCL_BOOLEAN_ATTRIBUTE_TYPE, 1 };
        return true;
    case MATTER_DYN_UINT8:
        info = { ZCL_INT8U_ATTRIBUTE_TYPE, 1 };
        return true;
    case MATTER_DYN_UINT16:
        info = { ZCL_INT16U_ATTRIBUTE_TYPE, 2 };
        return true;
    case MATTER_DYN_UINT32:
        info = { ZCL_INT32U_ATTRIBUTE_TYPE, 4 };
        return true;
    case MATTER_DYN_INT8:
        info = { ZCL_INT8S_ATTRIBUTE_TYPE, 1 };
        return true;
    case MATTER_DYN_INT16:
        info = { ZCL_INT16S_ATTRIBUTE_TYPE, 2 };
        return true;
    case MATTER_DYN_INT32:
        info = { ZCL_INT32S_ATTRIBUTE_TYPE, 4 };
        return true;
    case MATTER_DYN_ENUM8:
        info = { ZCL_ENUM8_ATTRIBUTE_TYPE, 1 };
        return true;
    case MATTER_DYN_BITMAP8:
        info = { ZCL_BITMAP8_ATTRIBUTE_TYPE, 1 };
        return true;
    case MATTER_DYN_BITMAP32:
        info = { ZCL_BITMAP32_ATTRIBUTE_TYPE, 4 };
        return true;
    default:
        return false;
    }
}

// What an extension callback returned -> Interaction Model status. See
// "Callback results" in matter_dynamic_extension.h.
Status ToStatus(int rc)
{
    if (rc == 0)
    {
        return Status::Success;
    }
    return (rc > 0 && rc <= 0xFF) ? static_cast<Status>(rc) : Status::Failure;
}

EmberAfAttributeMetadata ExternalAttribute(AttributeId id, EmberAfAttributeType type, uint16_t size, EmberAfAttributeMask mask = 0)
{
    return { ZAP_EMPTY_DEFAULT(), id, size, type,
             static_cast<EmberAfAttributeMask>(mask | ZAP_ATTRIBUTE_MASK(EXTERNAL_STORAGE) | ZAP_ATTRIBUTE_MASK(READABLE)) };
}

// Hands a cluster's commands to the extension that provides it. Only created
// for clusters whose description has an invoke callback.
class ExtensionCommandHandler : public CommandHandlerInterface
{
public:
    ExtensionCommandHandler(EndpointId endpoint, const matter_dyn_cluster & cluster) :
        CommandHandlerInterface(Optional<EndpointId>(endpoint), cluster.id), mCluster(cluster)
    {}

    void InvokeCommand(HandlerContext & context) override
    {
        context.SetCommandHandled();

        matter_dyn_arg args[MATTER_DYN_MAX_ARGS];
        uint8_t argCount = 0;
        if (DecodeArgs(context.mPayload, args, argCount) != CHIP_NO_ERROR)
        {
            context.mCommandHandler.AddStatus(context.mRequestPath, Status::InvalidCommand);
            return;
        }

        int rc = mCluster.invoke(context.mRequestPath.mEndpointId, context.mRequestPath.mClusterId,
                                 context.mRequestPath.mCommandId, args, argCount);
        context.mCommandHandler.AddStatus(context.mRequestPath, ToStatus(rc));
    }

private:
    // The command's fields are a TLV structure of context-tagged elements;
    // the scalar ones are passed on, anything else (strings, lists, nested
    // structs) is skipped.
    static CHIP_ERROR DecodeArgs(TLV::TLVReader & reader, matter_dyn_arg * args, uint8_t & argCount)
    {
        VerifyOrReturnError(reader.GetType() == TLV::kTLVType_Structure, CHIP_ERROR_WRONG_TLV_TYPE);

        TLV::TLVType outer;
        ReturnErrorOnFailure(reader.EnterContainer(outer));

        CHIP_ERROR err;
        while ((err = reader.Next()) == CHIP_NO_ERROR)
        {
            if (!TLV::IsContextTag(reader.GetTag()) || argCount >= MATTER_DYN_MAX_ARGS)
            {
                continue;
            }

            matter_dyn_arg & arg = args[argCount];
            arg.tag              = static_cast<uint8_t>(TLV::TagNumFromTag(reader.GetTag()));
            arg.value            = 0;
            switch (reader.GetType())
            {
            case TLV::kTLVType_Null:
                arg.type = MATTER_DYN_ARG_NULL;
                break;
            case TLV::kTLVType_Boolean: {
                bool value = false;
                ReturnErrorOnFailure(reader.Get(value));
                arg.type  = MATTER_DYN_ARG_BOOL;
                arg.value = value;
                break;
            }
            case TLV::kTLVType_UnsignedInteger: {
                uint64_t value = 0;
                ReturnErrorOnFailure(reader.Get(value));
                arg.type  = MATTER_DYN_ARG_UINT;
                arg.value = static_cast<int64_t>(value);
                break;
            }
            case TLV::kTLVType_SignedInteger:
                ReturnErrorOnFailure(reader.Get(arg.value));
                arg.type = MATTER_DYN_ARG_INT;
                break;
            default:
                continue;
            }
            argCount++;
        }
        VerifyOrReturnError(err == CHIP_END_OF_TLV, err);

        return reader.ExitContainer(outer);
    }

    const matter_dyn_cluster & mCluster;
};

// One dynamic endpoint. The Ember structures registered with
// emberAfSetDynamicEndpoint() point into the vectors here, so those are
// filled once in Add() and never resized afterwards.
struct Entry
{
    const void * owner                     = nullptr;
    const matter_dyn_endpoint * description = nullptr; // nullptr: slot is free
    EndpointId id                          = kInvalidEndpointId;

    EmberAfEndpointType endpointType;
    std::vector<EmberAfCluster> clusters;
    std::vector<std::vector<EmberAfAttributeMetadata>> attributes;
    std::vector<std::vector<CommandId>> commands;
    std::vector<DataVersion> dataVersions;
    EmberAfDeviceType deviceTypes[2];

    std::vector<std::unique_ptr<ExtensionCommandHandler>> commandHandlers;
    LazyRegisteredServerCluster<BridgedDeviceBasicInformationCluster> bridgedInfo;

    const matter_dyn_cluster * FindCluster(ClusterId clusterId) const
    {
        for (uint8_t i = 0; i < description->cluster_count; i++)
        {
            if (description->clusters[i].id == clusterId)
            {
                return &description->clusters[i];
            }
        }
        return nullptr;
    }
};

Entry sEntries[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
const void * sCurrentOwner = nullptr;
EndpointId sFirstDynamicEndpointId;
EndpointId sNextEndpointId;

Entry * FindEntry(EndpointId endpoint)
{
    uint16_t index = emberAfGetDynamicIndexFromEndpoint(endpoint);
    VerifyOrReturnValue(index < MATTER_ARRAY_SIZE(sEntries) && sEntries[index].description != nullptr, nullptr);
    return &sEntries[index];
}

bool IsValid(const matter_dyn_endpoint * description)
{
    VerifyOrReturnValue(description != nullptr && description->name != nullptr, false);
    VerifyOrReturnValue(strlen(description->name) <= kMaxNameLength, false);
    VerifyOrReturnValue(description->clusters != nullptr && description->cluster_count > 0, false);

    for (uint8_t i = 0; i < description->cluster_count; i++)
    {
        const matter_dyn_cluster & cluster = description->clusters[i];
        // These two are ours to provide.
        VerifyOrReturnValue(cluster.id != Descriptor::Id && cluster.id != BridgedDeviceBasicInformation::Id, false);
        VerifyOrReturnValue(cluster.read != nullptr, false);
        VerifyOrReturnValue(cluster.attributes != nullptr || cluster.attribute_count == 0, false);
        VerifyOrReturnValue(cluster.commands != nullptr || cluster.command_count == 0, false);
        for (uint8_t j = 0; j < i; j++)
        {
            VerifyOrReturnValue(description->clusters[j].id != cluster.id, false);
        }
        for (uint8_t j = 0; j < cluster.attribute_count; j++)
        {
            const matter_dyn_attribute & attribute = cluster.attributes[j];
            TypeInfo info;
            VerifyOrReturnValue(GetTypeInfo(attribute.type, info), false);
            VerifyOrReturnValue(attribute.id != Globals::Attributes::ClusterRevision::Id &&
                                    attribute.id != Globals::Attributes::FeatureMap::Id,
                                false);
            VerifyOrReturnValue(!(attribute.flags & MATTER_DYN_WRITABLE) || cluster.write != nullptr, false);
        }
    }
    return true;
}

// Fills in the Ember description of the endpoint: the extension's clusters
// followed by a Descriptor cluster, the runtime equivalent of bridge-app's
// DECLARE_DYNAMIC_ATTRIBUTE_LIST/CLUSTER_LIST/ENDPOINT blocks.
void BuildEmberEndpoint(Entry & entry)
{
    const matter_dyn_endpoint & description = *entry.description;
    const size_t clusterCount               = static_cast<size_t>(description.cluster_count) + 1;

    entry.attributes.resize(clusterCount);
    entry.commands.resize(clusterCount);
    entry.clusters.resize(clusterCount);
    entry.dataVersions.assign(clusterCount, 0);

    for (uint8_t i = 0; i < description.cluster_count; i++)
    {
        const matter_dyn_cluster & cluster = description.clusters[i];

        auto & attributes = entry.attributes[i];
        for (uint8_t j = 0; j < cluster.attribute_count; j++)
        {
            const matter_dyn_attribute & attribute = cluster.attributes[j];
            TypeInfo info;
            GetTypeInfo(attribute.type, info);

            EmberAfAttributeMask mask = 0;
            if (attribute.flags & MATTER_DYN_WRITABLE)
            {
                mask |= ZAP_ATTRIBUTE_MASK(WRITABLE);
            }
            if (attribute.flags & MATTER_DYN_NULLABLE)
            {
                mask |= ZAP_ATTRIBUTE_MASK(NULLABLE);
            }
            attributes.push_back(ExternalAttribute(attribute.id, info.zclType, info.size, mask));
        }
        attributes.push_back(ExternalAttribute(Globals::Attributes::FeatureMap::Id, ZCL_BITMAP32_ATTRIBUTE_TYPE, 4));
        attributes.push_back(ExternalAttribute(Globals::Attributes::ClusterRevision::Id, ZCL_INT16U_ATTRIBUTE_TYPE, 2));

        auto & commands = entry.commands[i];
        commands.assign(cluster.commands, cluster.commands + cluster.command_count);
        commands.push_back(kInvalidCommandId);

        entry.clusters[i] = { cluster.id,
                              attributes.data(),
                              static_cast<uint16_t>(attributes.size()),
                              0,
                              ZAP_CLUSTER_MASK(SERVER),
                              nullptr,
                              cluster.command_count > 0 ? commands.data() : nullptr,
                              nullptr };
    }

    auto & descriptorAttributes = entry.attributes[description.cluster_count];
    for (AttributeId id : { Descriptor::Attributes::DeviceTypeList::Id, Descriptor::Attributes::ServerList::Id,
                            Descriptor::Attributes::ClientList::Id, Descriptor::Attributes::PartsList::Id })
    {
        descriptorAttributes.push_back(ExternalAttribute(id, ZCL_ARRAY_ATTRIBUTE_TYPE, kDescriptorAttributeArraySize));
    }
    descriptorAttributes.push_back(ExternalAttribute(Globals::Attributes::ClusterRevision::Id, ZCL_INT16U_ATTRIBUTE_TYPE, 2));
    entry.clusters[description.cluster_count] = { Descriptor::Id,
                                                  descriptorAttributes.data(),
                                                  static_cast<uint16_t>(descriptorAttributes.size()),
                                                  0,
                                                  ZAP_CLUSTER_MASK(SERVER),
                                                  nullptr,
                                                  nullptr,
                                                  nullptr };

    entry.endpointType   = { entry.clusters.data(), static_cast<uint8_t>(clusterCount), 0 };
    entry.deviceTypes[0] = { description.device_type, description.device_type_revision };
    entry.deviceTypes[1] = { kBridgedNodeDeviceType, kBridgedNodeDeviceRevision };
}

void Release(uint16_t index)
{
    Entry & entry = sEntries[index];

    for (auto & handler : entry.commandHandlers)
    {
        CommandHandlerInterfaceRegistry::Instance().UnregisterCommandHandler(handler.get());
    }
    if (entry.bridgedInfo.IsConstructed())
    {
        LogErrorOnFailure(CodegenDataModelProvider::Instance().Registry().Unregister(&entry.bridgedInfo.Cluster()));
        entry.bridgedInfo.Destroy();
    }
    if (entry.id != kInvalidEndpointId)
    {
        emberAfClearDynamicEndpoint(index);
        ChipLogProgress(NotSpecified, "Dynamic: removed endpoint %u", entry.id);
    }

    entry.owner       = nullptr;
    entry.description = nullptr;
    entry.id          = kInvalidEndpointId;
    entry.commandHandlers.clear();
    entry.clusters.clear();
    entry.attributes.clear();
    entry.commands.clear();
    entry.dataVersions.clear();
}

} // namespace

namespace DynamicEndpointManager {

void Init()
{
    // As in bridge-app: dynamic endpoint IDs start after the last fixed
    // endpoint, and that last fixed endpoint only exists so that ZAP
    // generates the code for the clusters bridged devices may use -- it is
    // not a real device, so hide it.
    EndpointId lastFixedEndpoint = emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1));
    sFirstDynamicEndpointId      = static_cast<EndpointId>(lastFixedEndpoint + 1);
    sNextEndpointId              = sFirstDynamicEndpointId;
    emberAfEndpointEnableDisable(lastFixedEndpoint, false);
}

void SetCurrentOwner(const void * owner)
{
    sCurrentOwner = owner;
}

void RemoveAll(const void * owner)
{
    for (uint16_t index = 0; index < MATTER_ARRAY_SIZE(sEntries); index++)
    {
        if (sEntries[index].description != nullptr && sEntries[index].owner == owner)
        {
            Release(index);
        }
    }
}

size_t Count()
{
    size_t count = 0;
    for (const Entry & entry : sEntries)
    {
        count += entry.description != nullptr ? 1 : 0;
    }
    return count;
}

int Add(const matter_dyn_endpoint * description)
{
    VerifyOrReturnValue(sCurrentOwner != nullptr && IsValid(description), -EINVAL);

    uint16_t index = 0;
    while (index < MATTER_ARRAY_SIZE(sEntries) && sEntries[index].description != nullptr)
    {
        index++;
    }
    VerifyOrReturnValue(index < MATTER_ARRAY_SIZE(sEntries), -ENOSPC);

    Entry & entry     = sEntries[index];
    entry.owner       = sCurrentOwner;
    entry.description = description;
    BuildEmberEndpoint(entry);

    // Same endpoint ID allocation as bridge-app: keep counting up, skipping
    // IDs that are still in use, and never reuse the fixed endpoints' range.
    CHIP_ERROR err;
    while (true)
    {
        EndpointId candidate = sNextEndpointId;
        if (++sNextEndpointId < sFirstDynamicEndpointId)
        {
            sNextEndpointId = sFirstDynamicEndpointId;
        }
        err = emberAfSetDynamicEndpoint(index, candidate, &entry.endpointType,
                                        Span<DataVersion>(entry.dataVersions.data(), entry.dataVersions.size()),
                                        Span<const EmberAfDeviceType>(entry.deviceTypes), kAggregatorEndpointId);
        if (err == CHIP_NO_ERROR)
        {
            entry.id = candidate;
            break;
        }
        if (err != CHIP_ERROR_ENDPOINT_EXISTS)
        {
            ChipLogError(NotSpecified, "Dynamic: emberAfSetDynamicEndpoint failed: %" CHIP_ERROR_FORMAT, err.Format());
            Release(index);
            return -ENOMEM;
        }
    }

    // For some standard clusters the SDK has a server of its own that keeps
    // the attribute values itself (Temperature Measurement, for one). The
    // data model's placeholder endpoint pulls those servers into the build,
    // and they attach themselves to any endpoint that has their cluster --
    // including this one, where they would answer every read with their own
    // (empty) state instead of asking the extension. The cluster is the
    // extension's here, so take such a server out again; reads and writes
    // then go through Ember's external attribute storage like for any other
    // cluster. (Servers still built on Ember itself, like On/Off, don't
    // register this way and keep handling their commands.)
    auto & registry = CodegenDataModelProvider::Instance().Registry();
    for (uint8_t i = 0; i < description->cluster_count; i++)
    {
        if (ServerClusterInterface * sdkServer = registry.Get({ entry.id, description->clusters[i].id }); sdkServer != nullptr)
        {
            LogErrorOnFailure(registry.Unregister(sdkServer));
        }
    }

    // A bridged node must carry Bridged Device Basic Information.
    static DefaultTimerDelegate sTimerDelegate;
    static BridgedDeviceBasicInformationDelegate sBridgedInfoDelegate;
    char uniqueId[32];
    snprintf(uniqueId, sizeof(uniqueId), "LLEXT-%04X-%08lX", entry.id, static_cast<unsigned long>(Crypto::GetRandU32()));
    entry.bridgedInfo.Create(entry.id,
                             BridgedDeviceBasicInformationCluster::MutableData{ .reachable = true, .nodeLabel = description->name },
                             BridgedDeviceBasicInformationCluster::FixedData{ .uniqueId = uniqueId },
                             BridgedDeviceBasicInformationCluster::Context{ .delegate      = sBridgedInfoDelegate,
                                                                            .timerDelegate = sTimerDelegate });
    LogErrorOnFailure(registry.Register(entry.bridgedInfo.Registration()));

    for (uint8_t i = 0; i < description->cluster_count; i++)
    {
        if (description->clusters[i].invoke != nullptr)
        {
            auto handler = std::make_unique<ExtensionCommandHandler>(entry.id, description->clusters[i]);
            LogErrorOnFailure(CommandHandlerInterfaceRegistry::Instance().RegisterCommandHandler(handler.get()));
            entry.commandHandlers.push_back(std::move(handler));
        }
    }

    ChipLogProgress(NotSpecified, "Dynamic: added endpoint %u \"%s\" (device type 0x%04lx, %u cluster(s))", entry.id,
                    description->name, static_cast<unsigned long>(description->device_type), description->cluster_count);
    return entry.id;
}

int Remove(EndpointId endpoint)
{
    uint16_t index = emberAfGetDynamicIndexFromEndpoint(endpoint);
    VerifyOrReturnValue(index < MATTER_ARRAY_SIZE(sEntries) && sEntries[index].description != nullptr, -ENOENT);
    // An extension may only remove what it added itself.
    VerifyOrReturnValue(sEntries[index].owner == sCurrentOwner, -ENOENT);

    Release(index);
    return 0;
}

void AttributeChanged(EndpointId endpoint, ClusterId cluster, AttributeId attribute)
{
    VerifyOrReturn(FindEntry(endpoint) != nullptr);
    MatterReportingAttributeChangeCallback(endpoint, cluster, attribute);
}

} // namespace DynamicEndpointManager

// The extension API (include/matter_dynamic_extension.h), exported to llext
// in DynamicExports.c.
extern "C" int matter_dyn_endpoint_add(const struct matter_dyn_endpoint * endpoint)
{
    return DynamicEndpointManager::Add(endpoint);
}

extern "C" int matter_dyn_endpoint_remove(uint16_t endpoint)
{
    return DynamicEndpointManager::Remove(endpoint);
}

extern "C" void matter_dyn_attribute_changed(uint16_t endpoint, uint32_t cluster, uint32_t attribute)
{
    DynamicEndpointManager::AttributeChanged(endpoint, cluster, attribute);
}

// All attributes of dynamic endpoints use external storage, i.e. Ember asks
// here for their values instead of keeping them itself.
Status emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                            const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer,
                                            uint16_t maxReadLength)
{
    const Entry * entry = FindEntry(endpoint);
    VerifyOrReturnValue(entry != nullptr, Status::Failure);
    const matter_dyn_cluster * cluster = entry->FindCluster(clusterId);
    VerifyOrReturnValue(cluster != nullptr, Status::Failure);
    VerifyOrReturnValue(attributeMetadata->size <= maxReadLength, Status::ResourceExhausted);

    switch (attributeMetadata->attributeId)
    {
    case Globals::Attributes::ClusterRevision::Id:
        memcpy(buffer, &cluster->revision, sizeof(cluster->revision));
        return Status::Success;
    case Globals::Attributes::FeatureMap::Id:
        memcpy(buffer, &cluster->feature_map, sizeof(cluster->feature_map));
        return Status::Success;
    default:
        return ToStatus(cluster->read(endpoint, clusterId, attributeMetadata->attributeId, buffer, attributeMetadata->size));
    }
}

Status emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                             const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer)
{
    const Entry * entry = FindEntry(endpoint);
    VerifyOrReturnValue(entry != nullptr, Status::Failure);
    const matter_dyn_cluster * cluster = entry->FindCluster(clusterId);
    VerifyOrReturnValue(cluster != nullptr, Status::Failure);
    VerifyOrReturnValue(cluster->write != nullptr, Status::UnsupportedWrite);

    return ToStatus(cluster->write(endpoint, clusterId, attributeMetadata->attributeId, buffer, attributeMetadata->size));
}
