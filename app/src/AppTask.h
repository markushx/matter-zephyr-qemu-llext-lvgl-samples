#pragma once

#include <lib/core/CHIPError.h>

class AppTask
{
public:
    static AppTask & Instance()
    {
        static AppTask instance;
        return instance;
    }

    CHIP_ERROR Init();
    void StartApp();
};
