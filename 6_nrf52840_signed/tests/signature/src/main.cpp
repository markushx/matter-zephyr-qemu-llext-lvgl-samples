/*
 * ztest suite for ExtensionSignature, the check that decides whether an extension
 * may run on the 6_nrf52840_signed sample. Uses images from imgtool, through
 * scripts/sign-llext.py (see generate_test_images.py), and CHIP's real crypto,
 * and changes them the way someone without the signing key could.
 */

#include "llext/ExtensionSignature.h"
#include "test_images.h"

#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <cstring>
#include <vector>

using ExtensionSignature::Elf;
using ExtensionSignature::Result;
using Image = std::vector<uint8_t>;

namespace {

constexpr size_t kHeaderSize          = 32;
constexpr size_t kOffsetHeaderSize    = 8;
constexpr size_t kOffsetProtectedSize = 10;
constexpr size_t kOffsetImageSize     = 12;
constexpr size_t kOffsetFlags         = 16;
constexpr uint16_t kTlvFirmwareId     = 0xa0;
constexpr uint16_t kTlvEcdsaSig       = 0x22;

template <size_t N>
Image Copy(const uint8_t (&array)[N])
{
    return Image(array, array + N);
}

Result Check(const Image & image, const chip::Crypto::P256PublicKey & key, uint32_t firmwareId = kFirmwareId)
{
    Elf elf;
    return ExtensionSignature::Verify(image.data(), image.size(), firmwareId, key, elf);
}

Result Check(const Image & image)
{
    return Check(image, chip::Crypto::P256PublicKey(kSigningKey));
}

size_t SignedSize(const Image & image)
{
    return kHeaderSize + sys_get_le32(&image[kOffsetImageSize]) + sys_get_le16(&image[kOffsetProtectedSize]);
}

// Where the value of a TLV in the unprotected area is.
size_t FindUnprotected(const Image & image, uint16_t type)
{
    size_t offset = SignedSize(image) + 4;
    while (offset < image.size())
    {
        if (sys_get_le16(&image[offset]) == type)
        {
            return offset + 4;
        }
        offset += 4 + sys_get_le16(&image[offset + 2]);
    }
    zassert_unreachable("no TLV of type 0x%x", type);
    return 0;
}

} // namespace

ZTEST(extension_signature, test_valid_image)
{
    const Image image = Copy(kSignedImage);
    const chip::Crypto::P256PublicKey key(kSigningKey);

    Elf elf;
    zassert_equal(ExtensionSignature::Verify(image.data(), image.size(), kFirmwareId, key, elf), Result::kValid);
    zassert_equal(elf.size, sizeof(kPayload));
    zassert_mem_equal(image.data() + elf.offset, kPayload, sizeof(kPayload));
    // llext runs it in place.
    zassert_equal(elf.offset % 8, 0);
}

ZTEST(extension_signature, test_unsigned_elf)
{
    zassert_equal(Check(Copy(kPayload)), Result::kMalformed);
}

ZTEST(extension_signature, test_other_key)
{
    zassert_equal(Check(Copy(kSignedImage), chip::Crypto::P256PublicKey(kOtherKey)), Result::kBadSignature);
}

ZTEST(extension_signature, test_other_firmware)
{
    zassert_equal(Check(Copy(kSignedImage), chip::Crypto::P256PublicKey(kSigningKey), kFirmwareId + 1), Result::kWrongFirmware);
}

ZTEST(extension_signature, test_no_firmware_id)
{
    zassert_equal(Check(Copy(kNoFirmwareIdImage)), Result::kWrongFirmware);
}

// Anyone can add to the unprotected TLVs, so that mustn't work for the firmware ID.
ZTEST(extension_signature, test_firmware_id_in_unprotected_tlvs)
{
    Image image = Copy(kNoFirmwareIdImage);
    uint8_t tlv[8];
    sys_put_le16(kTlvFirmwareId, &tlv[0]);
    sys_put_le16(sizeof(uint32_t), &tlv[2]);
    sys_put_le32(kFirmwareId, &tlv[4]);
    image.insert(image.end(), tlv, tlv + sizeof(tlv));

    const size_t total = SignedSize(image) + 2;
    sys_put_le16(sys_get_le16(&image[total]) + sizeof(tlv), &image[total]);

    zassert_equal(Check(image), Result::kWrongFirmware);
}

// Header, ELF and firmware ID alike, whichever way it fails.
ZTEST(extension_signature, test_every_signed_byte_counts)
{
    const Image original = Copy(kSignedImage);
    const size_t signedSize = SignedSize(original);
    for (size_t i = 0; i < signedSize; i++)
    {
        Image image = original;
        image[i] ^= 0x01;
        zassert_not_equal(Check(image), Result::kValid, "changing byte %u went unnoticed", static_cast<unsigned>(i));
    }
}

ZTEST(extension_signature, test_changed_signature)
{
    Image image = Copy(kSignedImage);
    image.back() ^= 0x01;
    zassert_equal(Check(image), Result::kBadSignature);

    image = Copy(kSignedImage);
    image[FindUnprotected(image, kTlvEcdsaSig)] ^= 0x01; // No DER sequence any more.
    zassert_equal(Check(image), Result::kBadSignature);
}

ZTEST(extension_signature, test_no_signature)
{
    Image image = Copy(kSignedImage);
    sys_put_le16(0x7fff, &image[FindUnprotected(image, kTlvEcdsaSig) - 4]);
    zassert_equal(Check(image), Result::kMalformed);
}

ZTEST(extension_signature, test_truncated)
{
    const Image original = Copy(kSignedImage);
    for (size_t size = 0; size < original.size(); size++)
    {
        const Image image(original.begin(), original.begin() + size);
        zassert_equal(Check(image), Result::kMalformed, "accepted the first %u bytes", static_cast<unsigned>(size));
    }
}

ZTEST(extension_signature, test_trailing_bytes)
{
    Image image = Copy(kSignedImage);
    image.push_back(0xff);
    zassert_equal(Check(image), Result::kMalformed);
}

ZTEST(extension_signature, test_sizes_outside_the_image)
{
    Image image = Copy(kSignedImage);
    sys_put_le32(0xffffffff, &image[kOffsetImageSize]);
    zassert_equal(Check(image), Result::kMalformed);

    image = Copy(kSignedImage);
    sys_put_le32(0, &image[kOffsetImageSize]);
    zassert_equal(Check(image), Result::kMalformed);

    image = Copy(kSignedImage);
    sys_put_le16(0xffff, &image[kOffsetProtectedSize]);
    zassert_equal(Check(image), Result::kMalformed);

    image = Copy(kSignedImage);
    sys_put_le16(0xffff, &image[SignedSize(image) + 2]);
    zassert_equal(Check(image), Result::kMalformed);
}

ZTEST(extension_signature, test_unsupported_header)
{
    Image image = Copy(kSignedImage);
    sys_put_le16(kHeaderSize * 2, &image[kOffsetHeaderSize]);
    zassert_equal(Check(image), Result::kMalformed);

    image = Copy(kSignedImage);
    image[kOffsetFlags] = 0x04;
    zassert_equal(Check(image), Result::kMalformed);
}

static void * Setup(void)
{
    zassert_equal(psa_crypto_init(), PSA_SUCCESS);
    return nullptr;
}

ZTEST_SUITE(extension_signature, nullptr, Setup, nullptr, nullptr, nullptr);
