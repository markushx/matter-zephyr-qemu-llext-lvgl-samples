#include "ExtensionSignature.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>

#include <zephyr/sys/byteorder.h>

using namespace chip;

namespace {

// MCUboot's image format, see bootutil/image.h. Everything is little-endian:
//
//   header | ELF | protected TLVs | unprotected TLVs
//   `------- signed --------------'
//
// Each TLV area starts with its magic and its total size, including those.
constexpr uint32_t kImageMagic      = 0x96f3b83d;
constexpr uint16_t kProtectedMagic  = 0x6908;
constexpr uint16_t kUnprotectedMagic = 0x6907;
constexpr size_t kTlvHeaderSize     = 4;
constexpr uint16_t kTlvEcdsaSig     = 0x22;

// scripts/sign-llext.py always asks for this size. A fixed one also keeps the ELF
// as aligned as the buffer it's in, because llext runs it in place.
constexpr size_t kHeaderSize = 32;

// In the protected TLVs. The value must match scripts/sign-llext.py.
constexpr uint16_t kTlvFirmwareId = 0xa0;

constexpr size_t kOffsetHeaderSize    = 8;
constexpr size_t kOffsetProtectedSize = 10;
constexpr size_t kOffsetImageSize     = 12;
constexpr size_t kOffsetFlags         = 16;

bool IsTlvArea(ByteSpan area, uint16_t magic)
{
    return area.size() >= kTlvHeaderSize && sys_get_le16(area.data()) == magic && sys_get_le16(area.data() + 2) == area.size();
}

// `area` must have passed IsTlvArea(). Stops at an entry that doesn't fit.
bool FindTlv(ByteSpan area, uint16_t type, ByteSpan & value)
{
    size_t offset = kTlvHeaderSize;
    while (area.size() - offset >= kTlvHeaderSize)
    {
        const uint16_t entryType = sys_get_le16(area.data() + offset);
        const size_t entrySize   = sys_get_le16(area.data() + offset + 2);
        offset += kTlvHeaderSize;
        VerifyOrReturnValue(entrySize <= area.size() - offset, false);

        if (entryType == type)
        {
            value = area.SubSpan(offset, entrySize);
            return true;
        }
        offset += entrySize;
    }
    return false;
}

} // namespace

namespace ExtensionSignature {

Result Verify(const uint8_t * image, size_t size, uint32_t firmwareId, const Crypto::P256PublicKey & key, Elf & elf)
{
    // Nothing here is to be believed before the signature checks out. This only
    // keeps the offsets inside the image, and the sizes are checked one at a time
    // so that none of the sums can overflow.
    VerifyOrReturnValue(size >= kHeaderSize, Result::kMalformed);
    VerifyOrReturnValue(sys_get_le32(image) == kImageMagic, Result::kMalformed);
    VerifyOrReturnValue(sys_get_le16(image + kOffsetHeaderSize) == kHeaderSize, Result::kMalformed);
    // These would be encryption, compression or the like.
    VerifyOrReturnValue(sys_get_le32(image + kOffsetFlags) == 0, Result::kMalformed);

    const size_t elfSize = sys_get_le32(image + kOffsetImageSize);
    VerifyOrReturnValue(elfSize > 0 && elfSize <= size - kHeaderSize, Result::kMalformed);

    const size_t protectedOffset = kHeaderSize + elfSize;
    const size_t protectedSize   = sys_get_le16(image + kOffsetProtectedSize);
    VerifyOrReturnValue(protectedSize <= size - protectedOffset, Result::kMalformed);

    const size_t signedSize = protectedOffset + protectedSize;
    const ByteSpan unprotectedTlvs(image + signedSize, size - signedSize);
    VerifyOrReturnValue(IsTlvArea(unprotectedTlvs, kUnprotectedMagic), Result::kMalformed);

    ByteSpan derSignature;
    VerifyOrReturnValue(FindTlv(unprotectedTlvs, kTlvEcdsaSig, derSignature), Result::kMalformed);

    Crypto::P256ECDSASignature signature;
    MutableByteSpan rawSignature(signature.Bytes(), signature.Capacity());
    VerifyOrReturnValue(Crypto::EcdsaAsn1SignatureToRaw(Crypto::kP256_FE_Length, derSignature, rawSignature) == CHIP_NO_ERROR,
                        Result::kBadSignature);
    VerifyOrReturnValue(signature.SetLength(rawSignature.size()) == CHIP_NO_ERROR, Result::kBadSignature);
    VerifyOrReturnValue(key.ECDSA_validate_msg_signature(image, signedSize, signature) == CHIP_NO_ERROR, Result::kBadSignature);

    // From here on it's what the signer wrote. Only look for the firmware ID in
    // the signed TLVs: anyone can add to the others.
    const ByteSpan protectedTlvs(image + protectedOffset, protectedSize);
    ByteSpan signedFirmwareId;
    VerifyOrReturnValue(IsTlvArea(protectedTlvs, kProtectedMagic), Result::kWrongFirmware);
    VerifyOrReturnValue(FindTlv(protectedTlvs, kTlvFirmwareId, signedFirmwareId), Result::kWrongFirmware);
    VerifyOrReturnValue(signedFirmwareId.size() == sizeof(uint32_t), Result::kWrongFirmware);
    VerifyOrReturnValue(sys_get_le32(signedFirmwareId.data()) == firmwareId, Result::kWrongFirmware);

    elf.offset = kHeaderSize;
    elf.size   = elfSize;
    return Result::kValid;
}

} // namespace ExtensionSignature
