// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cast_certificate/cast_cert_validator.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/memory/singleton.h"
#include "net/cert/internal/certificate_policies.h"
#include "net/cert/internal/extended_key_usage.h"
#include "net/cert/internal/parse_certificate.h"
#include "net/cert/internal/parse_name.h"
#include "net/cert/internal/signature_algorithm.h"
#include "net/cert/internal/signature_policy.h"
#include "net/cert/internal/verify_certificate_chain.h"
#include "net/cert/internal/verify_signed_data.h"
#include "net/der/input.h"

namespace cast_certificate {
namespace {

// -------------------------------------------------------------------------
// Cast trust anchors.
// -------------------------------------------------------------------------

// There are two trusted roots for Cast certificate chains:
//
//   (1) CN=Cast Root CA    (kCastRootCaDer)
//   (2) CN=Eureka Root CA  (kEurekaRootCaDer)
//
// These constants are defined by the files included next:

#include "components/cast_certificate/cast_root_ca_cert_der-inc.h"
#include "components/cast_certificate/eureka_root_ca_der-inc.h"

// Singleton for the Cast trust store.
class CastTrustStore {
 public:
  static CastTrustStore* GetInstance() {
    return base::Singleton<CastTrustStore,
                           base::LeakySingletonTraits<CastTrustStore>>::get();
  }

  static net::TrustStore& Get() { return GetInstance()->store_; }

 private:
  friend struct base::DefaultSingletonTraits<CastTrustStore>;

  CastTrustStore() {
    // Initialize the trust store with two root certificates.
    CHECK(store_.AddTrustedCertificateWithoutCopying(kCastRootCaDer,
                                                     sizeof(kCastRootCaDer)));
    CHECK(store_.AddTrustedCertificateWithoutCopying(kEurekaRootCaDer,
                                                     sizeof(kEurekaRootCaDer)));
  }

  net::TrustStore store_;
  DISALLOW_COPY_AND_ASSIGN(CastTrustStore);
};

using ExtensionsMap = std::map<net::der::Input, net::ParsedExtension>;

// Helper that looks up an extension by OID given a map of extensions.
bool GetExtensionValue(const ExtensionsMap& extensions,
                       const net::der::Input& oid,
                       net::der::Input* value) {
  auto it = extensions.find(oid);
  if (it == extensions.end())
    return false;
  *value = it->second.value;
  return true;
}

// Returns the OID for the Audio-Only Cast policy
// (1.3.6.1.4.1.11129.2.5.2) in DER form.
net::der::Input AudioOnlyPolicyOid() {
  static const uint8_t kAudioOnlyPolicy[] = {0x2B, 0x06, 0x01, 0x04, 0x01,
                                             0xD6, 0x79, 0x02, 0x05, 0x02};
  return net::der::Input(kAudioOnlyPolicy);
}

// Cast certificates rely on RSASSA-PKCS#1 v1.5 with SHA-1 for signatures.
//
// The following signature policy specifies which signature algorithms (and key
// sizes) are acceptable. It is used when verifying a chain of certificates, as
// well as when verifying digital signature using the target certificate's
// SPKI.
//
// This particular policy allows for:
//   * ECDSA, RSA-SSA, and RSA-PSS
//   * Supported EC curves: P-256, P-384, P-521.
//   * Hashes: All SHA hashes including SHA-1 (despite being known weak).
//   * RSA keys must have a modulus at least 2048-bits long.
std::unique_ptr<net::SignaturePolicy> CreateCastSignaturePolicy() {
  return base::WrapUnique(new net::SimpleSignaturePolicy(2048));
}

class CertVerificationContextImpl : public CertVerificationContext {
 public:
  // Save a copy of the passed in public key (DER) and common name (text).
  CertVerificationContextImpl(const net::der::Input& spki,
                              const base::StringPiece& common_name)
      : spki_(spki.AsString()), common_name_(common_name.as_string()) {}

  bool VerifySignatureOverData(const base::StringPiece& signature,
                               const base::StringPiece& data) const override {
    // This code assumes the signature algorithm was RSASSA PKCS#1 v1.5 with
    // SHA-1.
    // TODO(eroman): Is it possible to use other hash algorithms?
    auto signature_algorithm =
        net::SignatureAlgorithm::CreateRsaPkcs1(net::DigestAlgorithm::Sha1);

    // Use the same policy as was used for verifying signatures in
    // certificates. This will ensure for instance that the key used is at
    // least 2048-bits long.
    auto signature_policy = CreateCastSignaturePolicy();

    return net::VerifySignedData(
        *signature_algorithm, net::der::Input(data),
        net::der::BitString(net::der::Input(signature), 0),
        net::der::Input(&spki_), signature_policy.get());
  }

  std::string GetCommonName() const override { return common_name_; }

 private:
  std::string spki_;
  std::string common_name_;
};

// Helper that extracts the Common Name from a certificate's subject field. On
// success |common_name| contains the text for the attribute (unescaped, so
// will depend on the encoding used, but for Cast device certs it should
// be ASCII).
bool GetCommonNameFromSubject(const net::der::Input& subject_tlv,
                              std::string* common_name) {
  net::RDNSequence rdn_sequence;
  if (!net::ParseName(subject_tlv, &rdn_sequence))
    return false;

  for (const net::RelativeDistinguishedName& rdn : rdn_sequence) {
    for (const auto& atv : rdn) {
      if (atv.type == net::TypeCommonNameOid()) {
        return atv.ValueAsStringUnsafe(common_name);
      }
    }
  }
  return false;
}

// Returns true if the extended key usage list |ekus| contains client auth.
bool HasClientAuth(const std::vector<net::der::Input>& ekus) {
  for (const auto& oid : ekus) {
    if (oid == net::ClientAuth())
      return true;
  }
  return false;
}

// Checks properties on the target certificate.
//
//   * The Key Usage must include Digital Signature
//   * THe Extended Key Usage must includ TLS Client Auth
//   * May have the policy 1.3.6.1.4.1.11129.2.5.2 to indicate it
//     is an audio-only device.
WARN_UNUSED_RESULT bool CheckTargetCertificate(
    const net::der::Input& cert_der,
    std::unique_ptr<CertVerificationContext>* context,
    CastDeviceCertPolicy* policy) {
  // TODO(eroman): Simplify this. The certificate chain verification
  // function already parses this stuff, awkward to re-do it here.

  net::ParsedCertificate cert;
  if (!net::ParseCertificate(cert_der, &cert))
    return false;

  net::ParsedTbsCertificate tbs;
  if (!net::ParseTbsCertificate(cert.tbs_certificate_tlv, &tbs))
    return false;

  // Get the extensions.
  if (!tbs.has_extensions)
    return false;
  ExtensionsMap extensions;
  if (!net::ParseExtensions(tbs.extensions_tlv, &extensions))
    return false;

  net::der::Input extension_value;

  // Get the Key Usage extension.
  if (!GetExtensionValue(extensions, net::KeyUsageOid(), &extension_value))
    return false;
  net::der::BitString key_usage;
  if (!net::ParseKeyUsage(extension_value, &key_usage))
    return false;

  // Ensure Key Usage contains digitalSignature.
  if (!key_usage.AssertsBit(net::KEY_USAGE_BIT_DIGITAL_SIGNATURE))
    return false;

  // Get the Extended Key Usage extension.
  if (!GetExtensionValue(extensions, net::ExtKeyUsageOid(), &extension_value))
    return false;
  std::vector<net::der::Input> ekus;
  if (!net::ParseEKUExtension(extension_value, &ekus))
    return false;

  // Ensure Extended Key Usage contains client auth.
  if (!HasClientAuth(ekus))
    return false;

  // Check for an optional audio-only policy extension.
  *policy = CastDeviceCertPolicy::NONE;
  if (GetExtensionValue(extensions, net::CertificatePoliciesOid(),
                        &extension_value)) {
    std::vector<net::der::Input> policies;
    if (!net::ParseCertificatePoliciesExtension(extension_value, &policies))
      return false;

    // Look for an audio-only policy. Disregard any other policy found.
    if (std::find(policies.begin(), policies.end(), AudioOnlyPolicyOid()) !=
        policies.end()) {
      *policy = CastDeviceCertPolicy::AUDIO_ONLY;
    }
  }

  // Get the Common Name for the certificate.
  std::string common_name;
  if (!GetCommonNameFromSubject(tbs.subject_tlv, &common_name))
    return false;

  context->reset(new CertVerificationContextImpl(tbs.spki_tlv, common_name));
  return true;
}

// Converts a base::Time::Exploded to a net::der::GeneralizedTime.
net::der::GeneralizedTime ConvertExplodedTime(
    const base::Time::Exploded& exploded) {
  net::der::GeneralizedTime result;
  result.year = exploded.year;
  result.month = exploded.month;
  result.day = exploded.day_of_month;
  result.hours = exploded.hour;
  result.minutes = exploded.minute;
  result.seconds = exploded.second;
  return result;
}

}  // namespace

bool VerifyDeviceCert(const std::vector<std::string>& certs,
                      const base::Time::Exploded& time,
                      std::unique_ptr<CertVerificationContext>* context,
                      CastDeviceCertPolicy* policy) {
  // The underlying verification function expects a sequence of
  // der::Input, so wrap the data in it (cheap).
  std::vector<net::der::Input> input_chain;
  for (const auto& cert : certs)
    input_chain.push_back(net::der::Input(&cert));

  // Use a signature policy compatible with Cast's PKI.
  auto signature_policy = CreateCastSignaturePolicy();

  // Do RFC 5280 compatible certificate verification using the two Cast
  // trust anchors and Cast signature policy.
  if (!net::VerifyCertificateChain(input_chain, CastTrustStore::Get(),
                                   signature_policy.get(),
                                   ConvertExplodedTime(time))) {
    return false;
  }

  // Check properties of the leaf certificate (key usage, policy), and construct
  // a CertVerificationContext that uses its public key.
  return CheckTargetCertificate(input_chain[0], context, policy);
}

std::unique_ptr<CertVerificationContext> CertVerificationContextImplForTest(
    const base::StringPiece& spki) {
  // Use a bogus CommonName, since this is just exposed for testing signature
  // verification by unittests.
  return base::WrapUnique(
      new CertVerificationContextImpl(net::der::Input(spki), "CommonName"));
}

bool AddTrustAnchorForTest(const uint8_t* data, size_t length) {
  return CastTrustStore::Get().AddTrustedCertificateWithoutCopying(data,
                                                                   length);
}

}  // namespace cast_certificate
