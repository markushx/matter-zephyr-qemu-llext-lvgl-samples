// The Aggregator endpoint of the bridge-app data model carries the Actions
// cluster, which needs a delegate. This bridge defines no actions (rooms,
// scenes, ...) -- examples/bridge-app/linux/bridged-actions-stub.cpp shows
// what a real one looks like -- so the delegate reports empty lists and
// knows no action IDs.

#include <app/clusters/actions-server/actions-server.h>
#include <app/util/attribute-storage.h>
#include <lib/support/CodeUtils.h>

#include <memory>

using namespace chip;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::Actions;
using chip::Protocols::InteractionModel::Status;

namespace {

class NoActionsDelegate : public Actions::Delegate
{
public:
    CHIP_ERROR ReadActionAtIndex(uint16_t index, ActionStructStorage & action) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR ReadEndpointListAtIndex(uint16_t index, EndpointListStorage & epList) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    bool HaveActionWithId(uint16_t actionId, uint16_t & actionIndex) override { return false; }

    Status HandleInstantAction(uint16_t actionId, Optional<uint32_t> invokeId) override { return Status::NotFound; }
    Status HandleInstantActionWithTransition(uint16_t actionId, uint16_t transitionTime, Optional<uint32_t> invokeId) override
    {
        return Status::NotFound;
    }
    Status HandleStartAction(uint16_t actionId, Optional<uint32_t> invokeId) override { return Status::NotFound; }
    Status HandleStartActionWithDuration(uint16_t actionId, uint32_t duration, Optional<uint32_t> invokeId) override
    {
        return Status::NotFound;
    }
    Status HandleStopAction(uint16_t actionId, Optional<uint32_t> invokeId) override { return Status::NotFound; }
    Status HandlePauseAction(uint16_t actionId, Optional<uint32_t> invokeId) override { return Status::NotFound; }
    Status HandlePauseActionWithDuration(uint16_t actionId, uint32_t duration, Optional<uint32_t> invokeId) override
    {
        return Status::NotFound;
    }
    Status HandleResumeAction(uint16_t actionId, Optional<uint32_t> invokeId) override { return Status::NotFound; }
    Status HandleEnableAction(uint16_t actionId, Optional<uint32_t> invokeId) override { return Status::NotFound; }
    Status HandleEnableActionWithDuration(uint16_t actionId, uint32_t duration, Optional<uint32_t> invokeId) override
    {
        return Status::NotFound;
    }
    Status HandleDisableAction(uint16_t actionId, Optional<uint32_t> invokeId) override { return Status::NotFound; }
    Status HandleDisableActionWithDuration(uint16_t actionId, uint32_t duration, Optional<uint32_t> invokeId) override
    {
        return Status::NotFound;
    }
};

NoActionsDelegate sDelegate;
std::unique_ptr<Actions::ActionsServer> sActionsServer;

} // namespace

void emberAfActionsClusterInitCallback(EndpointId endpoint)
{
    VerifyOrReturn(emberAfContainsServer(endpoint, Actions::Id) && !sActionsServer);

    sActionsServer = std::make_unique<Actions::ActionsServer>(endpoint, sDelegate);
    LogErrorOnFailure(sActionsServer->Init());
}
