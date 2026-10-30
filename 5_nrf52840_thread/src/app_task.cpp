#include "app_task.h"

#include "app/matter_init.h"
#include "app/task_executor.h"
#include "board/board.h"
#include "dynamic/DynamicEndpointManager.h"
#include "llext/ExtensionLoaderCluster.h"

#include <app/server-cluster/ServerClusterInterfaceRegistry.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace chip;
using namespace chip::app;

namespace {

CHIP_ERROR PostServerInit()
{
    DynamicEndpointManager::Init();

    static RegisteredServerCluster<ExtensionLoaderCluster> sExtensionLoaderCluster;
    return CodegenDataModelProvider::Instance().Registry().Register(sExtensionLoaderCluster.Registration());
}

} // namespace

CHIP_ERROR AppTask::Init()
{
    ReturnErrorOnFailure(Nrf::Matter::PrepareServer(Nrf::Matter::InitData{ .mPostServerInitClbk = PostServerInit }));

    if (!Nrf::GetBoard().Init())
    {
        LOG_ERR("User interface initialization failed.");
        return CHIP_ERROR_INCORRECT_STATE;
    }

    ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(Nrf::Board::DefaultMatterEventHandler, 0));

    return Nrf::Matter::StartServer();
}

CHIP_ERROR AppTask::StartApp()
{
    ReturnErrorOnFailure(Init());

    while (true)
    {
        Nrf::DispatchNextTask();
    }

    return CHIP_NO_ERROR;
}
