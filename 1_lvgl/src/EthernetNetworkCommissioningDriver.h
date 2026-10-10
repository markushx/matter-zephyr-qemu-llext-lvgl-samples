#pragma once

#include <platform/NetworkCommissioning.h>

namespace chip {
namespace DeviceLayer {
namespace NetworkCommissioning {

// Minimal Ethernet driver for the qemu_x86 e1000 NIC: there is exactly one,
// always-connected interface, so most of BaseDriver's operations are no-ops.
class QemuEthernetDriver : public EthernetDriver
{
public:
    class QemuEthernetIterator final : public NetworkIterator
    {
    public:
        size_t Count() override { return 1; }
        bool Next(Network & item) override
        {
            if (mExhausted)
            {
                return false;
            }
            mExhausted = true;

            constexpr char kInterfaceName[] = "eth0";
            memcpy(item.networkID, kInterfaceName, sizeof(kInterfaceName) - 1);
            item.networkIDLen = sizeof(kInterfaceName) - 1;
            item.connected    = true;
            return true;
        }
        void Release() override { delete this; }

    private:
        bool mExhausted = false;
    };

    uint8_t GetMaxNetworks() override { return 1; }
    NetworkIterator * GetNetworks() override { return new QemuEthernetIterator(); }
};

} // namespace NetworkCommissioning
} // namespace DeviceLayer
} // namespace chip
