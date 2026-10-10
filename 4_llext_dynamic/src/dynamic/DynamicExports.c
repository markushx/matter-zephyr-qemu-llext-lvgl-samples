/*
 * The app -> extension half of include/matter_dynamic_extension.h: llext can
 * only resolve symbols the app explicitly exports. Kept in a C file since
 * EXPORT_SYMBOL is a C construct; the functions themselves are defined
 * (extern "C") in DynamicEndpointManager.cpp.
 */

#include "matter_dynamic_extension.h"

#include <zephyr/llext/symbol.h>

EXPORT_SYMBOL(matter_dyn_endpoint_add);
EXPORT_SYMBOL(matter_dyn_endpoint_remove);
EXPORT_SYMBOL(matter_dyn_attribute_changed);
