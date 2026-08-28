#include "LightingManager.h"
#include "gui/BulbUi.h"

#include <lib/support/logging/CHIPLogging.h>

void LightingManager::Set(bool onOff)
{
    mOnOff = onOff;
    ChipLogProgress(NotSpecified, "BULB: %s level=%u", mOnOff ? "ON" : "OFF", mLevel);
    BulbUi::SetState(mOnOff, mLevel);
}

void LightingManager::SetLevel(uint8_t level)
{
    mLevel = level;
    ChipLogProgress(NotSpecified, "BULB: %s level=%u", mOnOff ? "ON" : "OFF", mLevel);
    BulbUi::SetState(mOnOff, mLevel);
}
