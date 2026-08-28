#include "LightingManager.h"

#include <lib/support/logging/CHIPLogging.h>

#if defined(CONFIG_LVGL)
#include "gui/BulbUi.h"
#endif

void LightingManager::Set(bool onOff)
{
    mOnOff = onOff;
    ChipLogProgress(NotSpecified, "BULB: %s level=%u", mOnOff ? "ON" : "OFF", mLevel);
#if defined(CONFIG_LVGL)
    BulbUi::SetState(mOnOff, mLevel);
#endif
}

void LightingManager::SetLevel(uint8_t level)
{
    mLevel = level;
    ChipLogProgress(NotSpecified, "BULB: %s level=%u", mOnOff ? "ON" : "OFF", mLevel);
#if defined(CONFIG_LVGL)
    BulbUi::SetState(mOnOff, mLevel);
#endif
}
