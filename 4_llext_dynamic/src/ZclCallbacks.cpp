#include <app/ConcreteAttributePath.h>
#include <lib/support/logging/CHIPLogging.h>

// This app has no device behaviour of its own: the only attributes that live
// in the app's own attribute store belong to the root node and the
// Aggregator. Everything on the dynamic endpoints uses external storage and
// goes to the extension that provides it (src/dynamic/), not through here.
void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value)
{
    ChipLogDetail(NotSpecified, "Attribute changed: endpoint %u cluster 0x%08lx attribute 0x%08lx", attributePath.mEndpointId,
                  static_cast<unsigned long>(attributePath.mClusterId), static_cast<unsigned long>(attributePath.mAttributeId));
}
