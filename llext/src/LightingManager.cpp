#include "LightingManager.h"

#include <lib/support/logging/CHIPLogging.h>

void LightingManager::Set(bool onOff)
{
    mOnOff = onOff;
    ChipLogProgress(NotSpecified, "BULB: %s level=%u", mOnOff ? "ON" : "OFF", mLevel);
}

void LightingManager::SetLevel(uint8_t level)
{
    mLevel = level;
    ChipLogProgress(NotSpecified, "BULB: %s level=%u", mOnOff ? "ON" : "OFF", mLevel);
}
