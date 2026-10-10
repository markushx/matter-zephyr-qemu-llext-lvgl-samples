#include "AppTask.h"

#include <lib/support/logging/CHIPLogging.h>

int main()
{
    CHIP_ERROR err = AppTask::Instance().Init();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "AppTask::Init failed: %" CHIP_ERROR_FORMAT, err.Format());
        return -1;
    }

    AppTask::Instance().StartApp();
    return 0;
}
