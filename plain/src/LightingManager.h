#pragma once

#include <cstdint>

class LightingManager
{
public:
    static LightingManager & GetInstance()
    {
        static LightingManager instance;
        return instance;
    }

    void Set(bool onOff);
    void SetLevel(uint8_t level);
    bool IsTurnedOn() const { return mOnOff; }
    uint8_t GetLevel() const { return mLevel; }

private:
    bool mOnOff     = false;
    uint8_t mLevel = 254;
};
