#pragma once

#include <crypto/CHIPCryptoPAL.h>

#include <cstddef>
#include <cstdint>

// Decides whether an extension may run. An extension has full privileges, and
// Matter's Administer privilege only says who may send one, so what gets loaded
// has to carry a signature from whoever holds the signing key.
//
// A signed extension is an image from MCUboot's imgtool (see
// scripts/sign-llext.py): a header, the ELF and the signature over both, with the
// firmware the extension was built for in the signed part. Check it while the
// bytes are still as they arrived: llext_load() parses the ELF and relocates it
// in place, and that parser shouldn't see anything unsigned.
namespace ExtensionSignature {

enum class Result : uint8_t
{
    kValid,
    // Not a signed extension at all, an unsigned ELF for example.
    kMalformed,
    kBadSignature,
    // Properly signed, but for another firmware. Extensions are only linked
    // against the firmware by symbol name, so loading one on a firmware with a
    // different struct layout could go unnoticed and crash.
    kWrongFirmware,
};

// Where the ELF is inside the image.
struct Elf
{
    size_t offset = 0;
    size_t size   = 0;
};

// `elf` is only set when this returns kValid.
Result Verify(const uint8_t * image, size_t size, uint32_t firmwareId, const chip::Crypto::P256PublicKey & key, Elf & elf);

} // namespace ExtensionSignature
