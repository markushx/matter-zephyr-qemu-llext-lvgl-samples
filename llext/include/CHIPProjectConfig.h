#pragma once

// Use a default pairing code if one hasn't been provisioned in flash.
#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE 20202021
#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR 0xF00

// Exchange Manager / Secure Channel / interaction-model (DMG) logging is
// extremely chatty at Progress/Detail (every message send/receive, every
// attribute read) and drowns out ZCL's own logging in the 16KB ring buffer.
// Keep Error-level diagnostics from all three; drop Progress/Detail. ZCL
// (and everything else) is left at the global log filter.
#define CHIP_CONFIG_LOG_MODULE_ExchangeManager_PROGRESS 0
#define CHIP_CONFIG_LOG_MODULE_ExchangeManager_DETAIL 0
#define CHIP_CONFIG_LOG_MODULE_SecureChannel_PROGRESS 0
#define CHIP_CONFIG_LOG_MODULE_SecureChannel_DETAIL 0
#define CHIP_CONFIG_LOG_MODULE_DataManagement_PROGRESS 0
#define CHIP_CONFIG_LOG_MODULE_DataManagement_DETAIL 0
