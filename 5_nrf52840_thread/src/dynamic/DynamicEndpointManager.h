#pragma once

#include "matter_dynamic_extension.h"

#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/Span.h>

// Turns the endpoint descriptions extensions hand to matter_dyn_endpoint_add()
// (see include/matter_dynamic_extension.h) into Matter endpoints, and routes
// attribute reads/writes and command invokes on them back to the extension.
//
// Same mechanism as connectedhomeip's examples/bridge-app/linux: each
// endpoint is an Ember "dynamic endpoint" below the Aggregator on endpoint 1,
// all of its attributes use external storage, and it carries the Descriptor
// and Bridged Device Basic Information clusters a bridged node needs. What
// bridge-app declares at compile time with DECLARE_DYNAMIC_* is built here at
// runtime from the extension's tables.
//
// Everything here must be called on the Matter thread.
namespace DynamicEndpointManager {

// The Aggregator endpoint of the bridge-app data model.
inline constexpr chip::EndpointId kAggregatorEndpointId = 1;

// Call once, after Server::Init().
void Init();

// Endpoints added from now on belong to `owner` (an opaque token; nullptr for
// "nobody", in which case matter_dyn_endpoint_add() fails). The
// ExtensionLoaderCluster sets this around every call into an extension.
// `uniqueIdSeed` goes into the endpoints' UniqueID, which a controller expects
// to stay the same across reboots.
void SetCurrentOwner(const void * owner, uint32_t uniqueIdSeed = 0);

// Until the owner is next set, added endpoints get these IDs, in this order,
// instead of new ones. The IDs must outlive that. A restored extension adds
// the same endpoints in the same order, and controllers know them by these IDs.
void SetRestoredEndpoints(chip::Span<const chip::EndpointId> endpoints);

// Fills `endpoints` with the IDs of the endpoints `owner` added, lowest first,
// and returns how many there are (at most `maxEndpoints`).
size_t GetEndpoints(const void * owner, chip::EndpointId * endpoints, size_t maxEndpoints);

// Removes every endpoint `owner` added. Must be called before the extension
// is unloaded: afterwards nothing references its tables or code any more.
void RemoveAll(const void * owner);

// Number of dynamic endpoints currently in use.
size_t Count();

// The functions behind the extension API of the same name.
int Add(const matter_dyn_endpoint * description);
int Remove(chip::EndpointId endpoint);
void AttributeChanged(chip::EndpointId endpoint, chip::ClusterId cluster, chip::AttributeId attribute);

} // namespace DynamicEndpointManager
