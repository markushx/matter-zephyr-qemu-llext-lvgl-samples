#include "AppTask.h"
#include "EthernetNetworkCommissioningDriver.h"
#include "dynamic/DynamicEndpointManager.h"
#include "llext/ExtensionLoaderCluster.h"

#include <app/TestEventTriggerDelegate.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <app/server/Server.h>
#include <app/server-cluster/ServerClusterInterfaceRegistry.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <data-model-providers/codegen/Instance.h>
#include <DeviceInfoProviderImpl.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

using namespace chip;
using namespace chip::app;
using namespace chip::Credentials;
using namespace chip::DeviceLayer;

namespace {
// Not function-local statics with no dependency on the CHIP/kernel stack being
// up yet -- safe to construct at global static-init time.
DeviceLayer::NetworkCommissioning::QemuEthernetDriver sEthernetDriver;
uint8_t sTestEventTriggerEnableKey[TestEventTriggerDelegate::kEnableKeyLength] = { 0 };
} // namespace

CHIP_ERROR AppTask::Init()
{
    ReturnErrorOnFailure(Platform::MemoryInit());
    ReturnErrorOnFailure(PlatformMgr().InitChipStack());

    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());

    // Function-local statics: their constructors call into Server::GetInstance()
    // et al (e.g. NetworkCommissioning::Instance's Context member initializer),
    // which must not run before InitChipStack() above -- as global (namespace-
    // scope) statics they'd construct before main() / the kernel is fully up,
    // corrupting memory (observed as a garbage-EIP crash in the idle thread).
    static CommonCaseDeviceServerInitParams sInitParams;
    static SimpleTestEventTriggerDelegate sTestEventTriggerDelegate;
    static Clusters::NetworkCommissioning::Instance sEthernetCommissioningInstance(0 /* endpoint */, &sEthernetDriver);

    ReturnErrorOnFailure(sInitParams.InitializeStaticResourcesBeforeServerInit());

    // Required by clusters that ship with our data model (e.g. User Label on
    // EP0) -- CreateRegistration() VerifyOrDie()s on this being non-null.
    static DeviceInfoProviderImpl sDeviceInfoProvider;
    sDeviceInfoProvider.SetStorageDelegate(sInitParams.persistentStorageDelegate);
    SetDeviceInfoProvider(&sDeviceInfoProvider);

    ReturnErrorOnFailure(sTestEventTriggerDelegate.Init(ByteSpan(sTestEventTriggerEnableKey)));
    sInitParams.testEventTriggerDelegate = &sTestEventTriggerDelegate;
    sInitParams.dataModelProvider        = CodegenDataModelProviderInstance(sInitParams.persistentStorageDelegate);

    ReturnErrorOnFailure(sEthernetCommissioningInstance.Init());

    ReturnErrorOnFailure(Server::GetInstance().Init(sInitParams));

    DynamicEndpointManager::Init();

    ConfigurationMgr().LogDeviceConfig();
    PrintOnboardingCodes(RendezvousInformationFlags(RendezvousInformationFlag::kOnNetwork));

    {
        static RegisteredServerCluster<ExtensionLoaderCluster> sExtensionLoaderCluster;
        ReturnErrorOnFailure(CodegenDataModelProvider::Instance().Registry().Register(sExtensionLoaderCluster.Registration()));
    }

    return CHIP_NO_ERROR;
}

void AppTask::StartApp()
{
    PlatformMgr().RunEventLoop();
}
