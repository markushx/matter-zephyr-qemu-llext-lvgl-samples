#pragma once

#include "matter_dynamic_extension.h"

#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

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
void SetCurrentOwner(const void * owner);

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
