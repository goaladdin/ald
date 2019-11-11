
#include <ripple/app/misc/ValidatorKeys.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/basics/base64.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <test/unit_test/SuiteJournal.h>
#include <string>
namespace ripple {
namespace test {
class ValidatorKeys_test : public beast::unit_test::suite
{
    const std::string seed = "shUwVw52ofnCUX5m7kPTKzJdr4HEH";
    const std::string tokenSecretStr =
        "paQmjZ37pKKPMrgadBLsuf9ab7Y7EUNzh27LQrZqoexpAs31nJi";
    const std::vector<std::string> tokenBlob = {
        "    "
        "eyJ2YWxpZGF0aW9uX3NlY3JldF9rZXkiOiI5ZWQ0NWY4NjYyNDFjYzE4YTI3NDdiNT\n",
        " \tQzODdjMDYyNTkwNzk3MmY0ZTcxOTAyMzFmYWE5Mzc0NTdmYTlkYWY2IiwibWFuaWZl "
        "    \n",
        "\tc3QiOiJKQUFBQUFGeEllMUZ0d21pbXZHdEgyaUNjTUpxQzlnVkZLaWxHZncxL3ZDeE"
        "\n",
        "\t "
        "hYWExwbGMyR25NaEFrRTFhZ3FYeEJ3RHdEYklENk9NU1l1TTBGREFscEFnTms4U0tG\t  "
        "\t\n",
        "bjdNTzJmZGtjd1JRSWhBT25ndTlzQUtxWFlvdUorbDJWMFcrc0FPa1ZCK1pSUzZQU2\n",
        "hsSkFmVXNYZkFpQnNWSkdlc2FhZE9KYy9hQVpva1MxdnltR21WcmxIUEtXWDNZeXd1\n",
        "NmluOEhBU1FLUHVnQkQ2N2tNYVJGR3ZtcEFUSGxHS0pkdkRGbFdQWXk1QXFEZWRGdj\n",
        "VUSmEydzBpMjFlcTNNWXl3TFZKWm5GT3I3QzBrdzJBaVR6U0NqSXpkaXRROD0ifQ==\n"};
    const std::string tokenManifest =
        "JAAAAAFxIe1FtwmimvGtH2iCcMJqC9gVFKilGfw1/vCxHXXLplc2GnMhAkE1agqXxBwD"
        "wDbID6OMSYuM0FDAlpAgNk8SKFn7MO2fdkcwRQIhAOngu9sAKqXYouJ+l2V0W+sAOkVB"
        "+ZRS6PShlJAfUsXfAiBsVJGesaadOJc/aAZokS1vymGmVrlHPKWX3Yywu6in8HASQKPu"
        "gBD67kMaRFGvmpATHlGKJdvDFlWPYy5AqDedFv5TJa2w0i21eq3MYywLVJZnFOr7C0kw"
        "2AiTzSCjIzditQ8=";
    const std::vector<std::string> invalidTokenBlob = {
        "eyJtYW5pZmVzdCI6IkpBQUFBQVZ4SWUyOVVBdzViZFJudHJ1elVkREk4aDNGV1JWZl\n",
        "k3SXVIaUlKQUhJd3MxdzZzM01oQWtsa1VXQWR2RnFRVGRlSEpvS1pNY0hlS0RzOExo\n",
        "b3d3bDlHOEdkVGNJbmFka1l3UkFJZ0h2Q01lQU1aSzlqQnV2aFhlaFRLRzVDQ3BBR1\n",
        "k0bGtvZHRXYW84UGhzR3NDSUREVTA1d1c3bWNiMjlVNkMvTHBpZmgvakZPRGhFR21i\n",
        "NWF6dTJMVHlqL1pjQkpBbitmNGhtQTQ0U0tYbGtTTUFqak1rSWRyR1Rxa21SNjBzVG\n",
        "JaTjZOOUYwdk9UV3VYcUZ6eDFoSGIyL0RqWElVZXhDVGlITEcxTG9UdUp1eXdXbk55\n",
        "RFE9PSIsInZhbGlkYXRpb25fc2VjcmV0X2tleSI6IjkyRDhCNDBGMzYwMTc5MTkwMU\n",
        "MzQTUzMzI3NzBDMkUwMTA4MDI0NTZFOEM2QkI0NEQ0N0FFREQ0NzJGMDQ2RkYifQ==\n"};
public:
    void
    run() override
    {
        SuiteJournal journal ("ValidatorKeys_test", *this);
        SecretKey const seedSecretKey =
            generateSecretKey(KeyType::secp256k1, *parseBase58<Seed>(seed));
        PublicKey const seedPublicKey =
            derivePublicKey(KeyType::secp256k1, seedSecretKey);
        NodeID const seedNodeID = calcNodeID(seedPublicKey);
        auto const tokenSecretKey = *parseBase58<SecretKey>(
            TokenType::NodePrivate, tokenSecretStr);
        auto const tokenPublicKey =
            derivePublicKey(KeyType::secp256k1, tokenSecretKey);
        auto const m = deserializeManifest(base64_decode(tokenManifest));
        BEAST_EXPECT(m);
        NodeID const tokenNodeID = calcNodeID(m->masterKey);
        {
            Config c;
            ValidatorKeys k{c, journal};
            BEAST_EXPECT(k.publicKey.size() == 0);
            BEAST_EXPECT(k.manifest.empty());
            BEAST_EXPECT(!k.configInvalid());
        }
        {
            Config c;
            c.section(SECTION_VALIDATION_SEED).append(seed);
            ValidatorKeys k{c, journal};
            BEAST_EXPECT(k.publicKey == seedPublicKey);
            BEAST_EXPECT(k.secretKey == seedSecretKey);
            BEAST_EXPECT(k.nodeID == seedNodeID);
            BEAST_EXPECT(k.manifest.empty());
            BEAST_EXPECT(!k.configInvalid());
        }
        {
            Config c;
            c.section(SECTION_VALIDATION_SEED).append("badseed");
            ValidatorKeys k{c, journal};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(k.publicKey.size() == 0);
            BEAST_EXPECT(k.manifest.empty());
        }
        {
            Config c;
            c.section(SECTION_VALIDATOR_TOKEN).append(tokenBlob);
            ValidatorKeys k{c, journal};
            BEAST_EXPECT(k.publicKey == tokenPublicKey);
            BEAST_EXPECT(k.secretKey == tokenSecretKey);
            BEAST_EXPECT(k.nodeID == tokenNodeID);
            BEAST_EXPECT(k.manifest == tokenManifest);
            BEAST_EXPECT(!k.configInvalid());
        }
        {
            Config c;
            c.section(SECTION_VALIDATOR_TOKEN).append("badtoken");
            ValidatorKeys k{c, journal};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(k.publicKey.size() == 0);
            BEAST_EXPECT(k.manifest.empty());
        }
        {
            Config c;
            c.section(SECTION_VALIDATION_SEED).append(seed);
            c.section(SECTION_VALIDATOR_TOKEN).append(tokenBlob);
            ValidatorKeys k{c, journal};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(k.publicKey.size() == 0);
            BEAST_EXPECT(k.manifest.empty());
        }
        {
            Config c;
            c.section(SECTION_VALIDATOR_TOKEN).append(invalidTokenBlob);
            ValidatorKeys k{c, journal};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(k.publicKey.size() == 0);
            BEAST_EXPECT(k.manifest.empty());
        }
    }
};  
BEAST_DEFINE_TESTSUITE(ValidatorKeys, app, ripple);
}  
}  