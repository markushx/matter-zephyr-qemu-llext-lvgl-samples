#include "LightingManager.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip::app::Clusters;

namespace {
constexpr chip::EndpointId kLightEndpoint = 1;
} // namespace

void LightingManager::Init()
{
    // Both attributes are persisted, and MatterPostAttributeChangeCallback()
    // only fires on a *change* -- so without this, a bulb that was on before
    // a restart stays "off" here, and turning it on again changes nothing.
    bool onOff = false;
    if (OnOff::Attributes::OnOff::Get(kLightEndpoint, &onOff) == chip::Protocols::InteractionModel::Status::Success)
    {
        mOnOff = onOff;
    }
    chip::app::DataModel::Nullable<uint8_t> level;
    if (LevelControl::Attributes::CurrentLevel::Get(kLightEndpoint, level) == chip::Protocols::InteractionModel::Status::Success &&
        !level.IsNull())
    {
        mLevel = level.Value();
    }
    ChipLogProgress(NotSpecified, "BULB: %s level=%u (restored)", mOnOff ? "ON" : "OFF", mLevel);
}

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
