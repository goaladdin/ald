
#include <ripple/app/misc/Manifest.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/StringUtilities.h>
#include <test/jtx.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/app/main/DBInit.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/STExchange.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/utility/in_place_factory.hpp>
namespace ripple {
namespace test {
class Manifest_test : public beast::unit_test::suite
{
private:
    static PublicKey randomNode ()
    {
        return derivePublicKey (
            KeyType::secp256k1,
            randomSecretKey());
    }
    static PublicKey randomMasterKey ()
    {
        return derivePublicKey (
            KeyType::ed25519,
            randomSecretKey());
    }
    static void cleanupDatabaseDir (boost::filesystem::path const& dbPath)
    {
        using namespace boost::filesystem;
        if (!exists (dbPath) || !is_directory (dbPath) || !is_empty (dbPath))
            return;
        remove (dbPath);
    }
    static void setupDatabaseDir (boost::filesystem::path const& dbPath)
    {
        using namespace boost::filesystem;
        if (!exists (dbPath))
        {
            create_directory (dbPath);
            return;
        }
        if (!is_directory (dbPath))
        {
            Throw<std::runtime_error> ("Cannot create directory: " +
                                      dbPath.string ());
        }
    }
    static boost::filesystem::path getDatabasePath ()
    {
        return boost::filesystem::current_path () / "manifest_test_databases";
    }
public:
    Manifest_test ()
    {
        try
        {
            setupDatabaseDir (getDatabasePath ());
        }
        catch (std::exception const&)
        {
        }
    }
    ~Manifest_test ()
    {
        try
        {
            cleanupDatabaseDir (getDatabasePath ());
        }
        catch (std::exception const&)
        {
        }
    }
    std::string
    makeManifestString (
        PublicKey const& pk,
        SecretKey const& sk,
        PublicKey const& spk,
        SecretKey const& ssk,
        int seq)
    {
        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = spk;
        sign(st, HashPrefix::manifest, *publicKeyType(spk), ssk);
        sign(st, HashPrefix::manifest, *publicKeyType(pk), sk,
            sfMasterSignature);
        Serializer s;
        st.add(s);
        return base64_encode (std::string(
            static_cast<char const*> (s.data()), s.size()));
    }
    std::string
    makeRevocationString (
        SecretKey const& sk,
        KeyType type,
        bool invalidSig = false)
    {
        auto const pk = derivePublicKey(type, sk);
        STObject st(sfGeneric);
        st[sfSequence] = std::numeric_limits<std::uint32_t>::max ();
        st[sfPublicKey] = pk;
        sign(st, HashPrefix::manifest, type,
             invalidSig ? randomSecretKey() : sk, sfMasterSignature);
        BEAST_EXPECT(invalidSig ^ verify(
                         st, HashPrefix::manifest, pk, sfMasterSignature));
        Serializer s;
        st.add(s);
        return base64_encode (std::string(
            static_cast<char const*> (s.data()), s.size()));
    }
    Manifest
    makeRevocation (
        SecretKey const& sk, KeyType type, bool invalidSig = false)
    {
        auto const pk = derivePublicKey(type, sk);
        STObject st(sfGeneric);
        st[sfSequence] = std::numeric_limits<std::uint32_t>::max ();
        st[sfPublicKey] = pk;
        sign(st, HashPrefix::manifest, type,
            invalidSig ? randomSecretKey() : sk, sfMasterSignature);
        BEAST_EXPECT(invalidSig ^ verify(
            st, HashPrefix::manifest, pk, sfMasterSignature));
        Serializer s;
        st.add(s);
        std::string const m (static_cast<char const*> (s.data()), s.size());
        if (auto r = deserializeManifest(std::move(m)))
            return std::move(*r);
        Throw<std::runtime_error> ("Could not create a revocation manifest");
        return *deserializeManifest(std::move(m)); 
    }
    Manifest
    makeManifest (
        SecretKey const& sk, KeyType type, SecretKey const& ssk, KeyType stype,
         int seq, bool invalidSig = false)
    {
        auto const pk = derivePublicKey(type, sk);
        auto const spk = derivePublicKey(stype, ssk);
        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = spk;
        sign(st, HashPrefix::manifest, stype, ssk);
        BEAST_EXPECT(verify(st, HashPrefix::manifest, spk));
        sign(st, HashPrefix::manifest, type,
            invalidSig ? randomSecretKey() : sk, sfMasterSignature);
        BEAST_EXPECT(invalidSig ^ verify(
            st, HashPrefix::manifest, pk, sfMasterSignature));
        Serializer s;
        st.add(s);
        std::string const m (static_cast<char const*> (s.data()), s.size());
        if (auto r = deserializeManifest(std::move(m)))
            return std::move (*r);
        Throw<std::runtime_error> ("Could not create a manifest");
        return *deserializeManifest(std::move(m)); 
    }
    Manifest
    clone (Manifest const& m)
    {
        Manifest m2;
        m2.serialized = m.serialized;
        m2.masterKey = m.masterKey;
        m2.signingKey = m.signingKey;
        m2.sequence = m.sequence;
        m2.domain = m.domain;
        return m2;
    }
    void testLoadStore (ManifestCache& m)
    {
        testcase ("load/store");
        std::string const dbName("ManifestCacheTestDB");
        {
            DatabaseCon::Setup setup;
            setup.dataDir = getDatabasePath ();
            DatabaseCon dbCon(setup, dbName, WalletDBInit, WalletDBCount);
            auto getPopulatedManifests =
                [](ManifestCache const& cache) -> std::vector<Manifest const*>
                {
                    std::vector<Manifest const*> result;
                    result.reserve (32);
                    cache.for_each_manifest (
                        [&result](Manifest const& m) {result.push_back (&m);});
                    return result;
                };
            auto sort =
                [](std::vector<Manifest const*> mv) -> std::vector<Manifest const*>
                {
                    std::sort (mv.begin (), mv.end (),
                        [](Manifest const* lhs, Manifest const* rhs)
                        { return lhs->serialized < rhs->serialized; });
                    return mv;
                };
            std::vector<Manifest const*> const inManifests (
                sort (getPopulatedManifests (m)));
            jtx::Env env (*this);
            auto unl = std::make_unique<ValidatorList> (
                m, m, env.timeKeeper(), env.journal);
            {
                m.save (dbCon, "ValidatorManifests",
                    [&unl](PublicKey const& pubKey)
                    {
                        return unl->listed (pubKey);
                    });
                ManifestCache loaded;
                loaded.load (dbCon, "ValidatorManifests");
                std::vector<Manifest const*> const loadedManifests (
                    sort (getPopulatedManifests (loaded)));
                for (auto const& man : loadedManifests)
                    BEAST_EXPECT(man->revoked());
            }
            {
                PublicKey emptyLocalKey;
                std::vector<std::string> s1;
                std::vector<std::string> keys;
                std::string cfgManifest;
                for (auto const& man : inManifests)
                    s1.push_back (toBase58(
                        TokenType::NodePublic, man->masterKey));
                unl->load (emptyLocalKey, s1, keys);
                m.save (dbCon, "ValidatorManifests",
                    [&unl](PublicKey const& pubKey)
                    {
                        return unl->listed (pubKey);
                    });
                ManifestCache loaded;
                loaded.load (dbCon, "ValidatorManifests");
                std::vector<Manifest const*> const loadedManifests (
                    sort (getPopulatedManifests (loaded)));
                if (inManifests.size () == loadedManifests.size ())
                {
                    BEAST_EXPECT(std::equal
                            (inManifests.begin (), inManifests.end (),
                             loadedManifests.begin (),
                             [](Manifest const* lhs, Manifest const* rhs)
                             {return *lhs == *rhs;}));
                }
                else
                {
                    fail ();
                }
            }
            {
                ManifestCache loaded;
                std::vector<std::string> const emptyRevocation;
                std::string const badManifest = "bad manifest";
                BEAST_EXPECT(! loaded.load (
                    dbCon, "ValidatorManifests", badManifest, emptyRevocation));
                auto const sk  = randomSecretKey();
                auto const pk  = derivePublicKey(KeyType::ed25519, sk);
                auto const kp = randomKeyPair(KeyType::secp256k1);
                std::string const cfgManifest =
                    makeManifestString (pk, sk, kp.first, kp.second, 0);
                BEAST_EXPECT(loaded.load (
                    dbCon, "ValidatorManifests", cfgManifest, emptyRevocation));
            }
            {
                ManifestCache loaded;
                std::string const emptyManifest;
                std::vector<std::string> const badRevocation = { "bad revocation" };
                BEAST_EXPECT(! loaded.load (
                    dbCon, "ValidatorManifests", emptyManifest, badRevocation));
                auto const sk  = randomSecretKey();
                auto const keyType = KeyType::ed25519;
                auto const pk  = derivePublicKey(keyType, sk);
                auto const kp = randomKeyPair(KeyType::secp256k1);
                std::vector<std::string> const nonRevocation =
                    { makeManifestString (pk, sk, kp.first, kp.second, 0) };
                BEAST_EXPECT(! loaded.load (
                    dbCon, "ValidatorManifests", emptyManifest, nonRevocation));
                BEAST_EXPECT(! loaded.revoked(pk));
                std::vector<std::string> const badSigRevocation =
                    { makeRevocationString (sk, keyType, true) };
                BEAST_EXPECT(! loaded.load (
                    dbCon, "ValidatorManifests", emptyManifest, badSigRevocation));
                BEAST_EXPECT(! loaded.revoked(pk));
                std::vector<std::string> const cfgRevocation =
                    { makeRevocationString (sk, keyType) };
                BEAST_EXPECT(loaded.load (
                    dbCon, "ValidatorManifests", emptyManifest, cfgRevocation));
                BEAST_EXPECT(loaded.revoked(pk));
            }
        }
        boost::filesystem::remove (getDatabasePath () /
                                   boost::filesystem::path (dbName));
    }
    void testGetSignature()
    {
        testcase ("getSignature");
        auto const sk = randomSecretKey();
        auto const pk = derivePublicKey(KeyType::ed25519, sk);
        auto const kp = randomKeyPair(KeyType::secp256k1);
        auto const m = makeManifest (
            sk, KeyType::ed25519, kp.second, KeyType::secp256k1, 0);
        STObject st(sfGeneric);
        st[sfSequence] = 0;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = kp.first;
        Serializer ss;
        ss.add32(HashPrefix::manifest);
        st.addWithoutSigningFields(ss);
        auto const sig = sign(KeyType::secp256k1, kp.second, ss.slice());
        BEAST_EXPECT(strHex(sig) == strHex(*m.getSignature()));
        auto const masterSig = sign(KeyType::ed25519, sk, ss.slice());
        BEAST_EXPECT(strHex(masterSig) == strHex(m.getMasterSignature()));
    }
    void testGetKeys()
    {
        testcase ("getKeys");
        ManifestCache cache;
        auto const sk  = randomSecretKey();
        auto const pk  = derivePublicKey(KeyType::ed25519, sk);
        BEAST_EXPECT(cache.getSigningKey(pk) == pk);
        auto const kp0 = randomKeyPair(KeyType::secp256k1);
        BEAST_EXPECT(ManifestDisposition::accepted ==
            cache.applyManifest(makeManifest (
                sk, KeyType::ed25519, kp0.second, KeyType::secp256k1, 0)));
        BEAST_EXPECT(cache.getSigningKey(pk) == kp0.first);
        BEAST_EXPECT(cache.getMasterKey(kp0.first) == pk);
        auto const kp1 = randomKeyPair(KeyType::secp256k1);
        BEAST_EXPECT(ManifestDisposition::accepted ==
            cache.applyManifest(makeManifest (
                sk, KeyType::ed25519, kp1.second, KeyType::secp256k1, 1)));
        BEAST_EXPECT(cache.getSigningKey(pk) == kp1.first);
        BEAST_EXPECT(cache.getMasterKey(kp1.first) == pk);
        BEAST_EXPECT(cache.getMasterKey(kp0.first) == kp0.first);
        BEAST_EXPECT(ManifestDisposition::accepted ==
            cache.applyManifest(makeManifest (
                sk, KeyType::ed25519, kp1.second, KeyType::secp256k1, 2)));
        BEAST_EXPECT(cache.getSigningKey(pk) == kp1.first);
        BEAST_EXPECT(cache.getMasterKey(kp1.first) == pk);
        BEAST_EXPECT(cache.getMasterKey(kp0.first) == kp0.first);
        BEAST_EXPECT(ManifestDisposition::accepted ==
            cache.applyManifest(makeRevocation (
                sk, KeyType::ed25519)));
        BEAST_EXPECT(cache.revoked(pk));
        BEAST_EXPECT(cache.getSigningKey(pk) == pk);
        BEAST_EXPECT(cache.getMasterKey(kp0.first) == kp0.first);
        BEAST_EXPECT(cache.getMasterKey(kp1.first) == kp1.first);
    }
    void testValidatorToken()
    {
        testcase ("validator token");
        {
            auto const valSecret = parseBase58<SecretKey>(
                TokenType::NodePrivate,
                "paQmjZ37pKKPMrgadBLsuf9ab7Y7EUNzh27LQrZqoexpAs31nJi");
            std::vector<std::string> const tokenBlob = {
                "    eyJ2YWxpZGF0aW9uX3NlY3JldF9rZXkiOiI5ZWQ0NWY4NjYyNDFjYzE4YTI3NDdiNT\n",
                " \tQzODdjMDYyNTkwNzk3MmY0ZTcxOTAyMzFmYWE5Mzc0NTdmYTlkYWY2IiwibWFuaWZl     \n",
                "\tc3QiOiJKQUFBQUFGeEllMUZ0d21pbXZHdEgyaUNjTUpxQzlnVkZLaWxHZncxL3ZDeE\n",
                "\t hYWExwbGMyR25NaEFrRTFhZ3FYeEJ3RHdEYklENk9NU1l1TTBGREFscEFnTms4U0tG\t  \t\n",
                "bjdNTzJmZGtjd1JRSWhBT25ndTlzQUtxWFlvdUorbDJWMFcrc0FPa1ZCK1pSUzZQU2\n",
                "hsSkFmVXNYZkFpQnNWSkdlc2FhZE9KYy9hQVpva1MxdnltR21WcmxIUEtXWDNZeXd1\n",
                "NmluOEhBU1FLUHVnQkQ2N2tNYVJGR3ZtcEFUSGxHS0pkdkRGbFdQWXk1QXFEZWRGdj\n",
                "VUSmEydzBpMjFlcTNNWXl3TFZKWm5GT3I3QzBrdzJBaVR6U0NqSXpkaXRROD0ifQ==\n"
            };
            auto const manifest =
                "JAAAAAFxIe1FtwmimvGtH2iCcMJqC9gVFKilGfw1/vCxHXXLplc2GnMhAkE1agqXxBwD"
                "wDbID6OMSYuM0FDAlpAgNk8SKFn7MO2fdkcwRQIhAOngu9sAKqXYouJ+l2V0W+sAOkVB"
                "+ZRS6PShlJAfUsXfAiBsVJGesaadOJc/aAZokS1vymGmVrlHPKWX3Yywu6in8HASQKPu"
                "gBD67kMaRFGvmpATHlGKJdvDFlWPYy5AqDedFv5TJa2w0i21eq3MYywLVJZnFOr7C0kw"
                "2AiTzSCjIzditQ8=";
            auto const token = ValidatorToken::make_ValidatorToken(tokenBlob);
            BEAST_EXPECT(token);
            BEAST_EXPECT(token->validationSecret == *valSecret);
            BEAST_EXPECT(token->manifest == manifest);
        }
        {
            std::vector<std::string> const badToken = { "bad token" };
            BEAST_EXPECT(! ValidatorToken::make_ValidatorToken(badToken));
        }
    }
    void testManifestVersioning()
    {
        testcase ("Versioning");
        auto const sk = generateSecretKey (KeyType::ed25519, randomSeed ());
        auto const pk = derivePublicKey(KeyType::ed25519, sk);
        auto const ssk = generateSecretKey (KeyType::secp256k1, randomSeed ());
        auto const spk = derivePublicKey(KeyType::secp256k1, ssk);
        auto buildManifestObject = [&](std::uint16_t version)
        {
            STObject st(sfGeneric);
            st[sfSequence] = 3;
            st[sfPublicKey] = pk;
            st[sfSigningPubKey] = spk;
            if (version != 0)
                st[sfVersion] = version;
            sign(st, HashPrefix::manifest, KeyType::ed25519, sk, sfMasterSignature);
            sign(st, HashPrefix::manifest, KeyType::secp256k1, ssk);
            Serializer s;
            st.add(s);
            return std::string (static_cast<char const*>(s.data()), s.size());
        };
        BEAST_EXPECT(deserializeManifest(buildManifestObject(0)));
        BEAST_EXPECT(!deserializeManifest(buildManifestObject(1)));
        BEAST_EXPECT(!deserializeManifest(buildManifestObject(2001)));
    }
    void testManifestDeserialization()
    {
        std::array<KeyType, 2> const keyTypes {{
            KeyType::ed25519,
            KeyType::secp256k1 }};
        std::uint32_t sequence = 0;
        auto const ret = strUnHex("9930E7FC9D56BB25D6893BA3F317AE5BCF33B3291BD63DB32654A313222F7FD020");
        auto const badKey = Slice{ret.first.data(), ret.first.size()};
        auto const retShort = strUnHex("0330");
        auto const shortKey = Slice{retShort.first.data(), retShort.first.size()};
        auto toString = [](STObject const& st)
        {
            Serializer s;
            st.add(s);
            return std::string (static_cast<char const*> (s.data()), s.size());
        };
        for (auto const keyType : keyTypes)
        {
            auto const sk = generateSecretKey (keyType, randomSeed ());
            auto const pk = derivePublicKey(keyType, sk);
            for (auto const sKeyType : keyTypes)
            {
                auto const ssk = generateSecretKey (sKeyType, randomSeed ());
                auto const spk = derivePublicKey(sKeyType, ssk);
                auto buildManifestObject = [&](
                    std::uint32_t seq,
                    boost::optional<std::string> domain,
                    bool noSigningPublic = false,
                    bool noSignature = false)
                {
                    STObject st(sfGeneric);
                    st[sfSequence] = seq;
                    st[sfPublicKey] = pk;
                    if (domain)
                        st[sfDomain] = makeSlice(*domain);
                    if (! noSigningPublic)
                        st[sfSigningPubKey] = spk;
                    sign(st, HashPrefix::manifest, keyType, sk, sfMasterSignature);
                    if (! noSignature)
                        sign(st, HashPrefix::manifest, sKeyType, ssk);
                    return st;
                };
                {
                    testcase << "deserializeManifest: normal manifest (" <<
                        to_string(keyType) << " + " <<
                        to_string(sKeyType) << ")";
                    { 
                        auto const st = buildManifestObject(
                            ++sequence, boost::none);
                        auto const m = toString(st);
                        auto const manifest = deserializeManifest(m);
                        BEAST_EXPECT(manifest);
                        BEAST_EXPECT(manifest->masterKey == pk);
                        BEAST_EXPECT(manifest->signingKey == spk);
                        BEAST_EXPECT(manifest->sequence == sequence);
                        BEAST_EXPECT(manifest->serialized == m);
                        BEAST_EXPECT(manifest->domain.empty());
                        BEAST_EXPECT(manifest->verify());
                    }
                    { 
                        auto const st = buildManifestObject(
                            ++sequence, std::string{});
                        BEAST_EXPECT(!deserializeManifest(toString(st)));
                    }
                    { 
                        auto const st = buildManifestObject(
                            ++sequence, std::string{"a.b"});
                        BEAST_EXPECT(!deserializeManifest(toString(st)));
                    }
                    { 
                        std::string s(254, 'a');
                        auto const st = buildManifestObject(
                            ++sequence, s + ".example.com");
                        BEAST_EXPECT(!deserializeManifest(toString(st)));
                    }
                    { 
                        std::string s(72, 'a');
                        auto const st = buildManifestObject(
                            ++sequence, s + ".example.com");
                        BEAST_EXPECT(!deserializeManifest(toString(st)));
                    }
                    auto const st = buildManifestObject(
                        ++sequence, std::string{"example.com"});
                    {
                        auto const m = toString(st);
                        auto const manifest = deserializeManifest(m);
                        BEAST_EXPECT(manifest);
                        BEAST_EXPECT(manifest->masterKey == pk);
                        BEAST_EXPECT(manifest->signingKey == spk);
                        BEAST_EXPECT(manifest->sequence == sequence);
                        BEAST_EXPECT(manifest->serialized == m);
                        BEAST_EXPECT(manifest->domain == "example.com");
                        BEAST_EXPECT(manifest->verify());
                    }
                    {
                        auto badSigSt = st;
                        badSigSt[sfPublicKey] = badSigSt[sfSigningPubKey];
                        auto const m = toString(badSigSt);
                        auto const manifest = deserializeManifest(m);
                        BEAST_EXPECT(manifest);
                        BEAST_EXPECT(manifest->masterKey == spk);
                        BEAST_EXPECT(manifest->signingKey == spk);
                        BEAST_EXPECT(manifest->sequence == sequence);
                        BEAST_EXPECT(manifest->serialized == m);
                        BEAST_EXPECT(manifest->domain == "example.com");
                        BEAST_EXPECT(!manifest->verify());
                    }
                    {
                        auto badSt = st;
                        BEAST_EXPECT(badSt.delField(sfSequence));
                        BEAST_EXPECT(!deserializeManifest(toString(badSt)));
                    }
                    {
                        auto badSt = st;
                        BEAST_EXPECT(badSt.delField(sfPublicKey));
                        BEAST_EXPECT(!deserializeManifest(toString(badSt)));
                    }
                    {
                        auto badSt = st;
                        badSt[sfPublicKey] = badKey;
                        BEAST_EXPECT(!deserializeManifest(toString(badSt)));
                    }
                    {
                        auto badSt = st;
                        badSt[sfPublicKey] = shortKey;
                        BEAST_EXPECT(!deserializeManifest(toString(badSt)));
                    }
                    {
                        auto badSt = st;
                        BEAST_EXPECT(badSt.delField(sfSigningPubKey));
                        BEAST_EXPECT(!deserializeManifest(toString(badSt)));
                    }
                    {
                        auto badSt = st;
                        badSt[sfSigningPubKey] = badKey;
                        BEAST_EXPECT(!deserializeManifest(toString(badSt)));
                    }
                    {
                        auto badSt = st;
                        badSt[sfSigningPubKey] = shortKey;
                        BEAST_EXPECT(!deserializeManifest(toString(badSt)));
                    }
                    {
                        auto badSt = st;
                        BEAST_EXPECT(badSt.delField(sfMasterSignature));
                        BEAST_EXPECT(!deserializeManifest(toString(badSt)));
                    }
                    {
                        auto badSt = st;
                        BEAST_EXPECT(badSt.delField(sfSignature));
                        BEAST_EXPECT(!deserializeManifest(toString(badSt)));
                    }
                }
                {
                    testcase << "deserializeManifest: revocation manifest (" <<
                        to_string(keyType) << " + " <<
                        to_string(sKeyType) << ")";
                    {
                        auto const st = buildManifestObject(
                            std::numeric_limits<std::uint32_t>::max(),
                            boost::none, true, true);
                        auto const m = toString(st);
                        auto const manifest = deserializeManifest(m);
                        BEAST_EXPECT(manifest);
                        BEAST_EXPECT(manifest->masterKey == pk);
                        BEAST_EXPECT(manifest->signingKey == PublicKey());
                        BEAST_EXPECT(manifest->revoked());
                        BEAST_EXPECT(manifest->domain.empty());
                        BEAST_EXPECT(manifest->serialized == m);
                        BEAST_EXPECT(manifest->verify());
                    }
                    { 
                        auto const st = buildManifestObject(
                            std::numeric_limits<std::uint32_t>::max(),
                            boost::none, true, false);
                        BEAST_EXPECT(!deserializeManifest(toString(st)));
                    }
                    { 
                        auto const st = buildManifestObject(
                            std::numeric_limits<std::uint32_t>::max(),
                            boost::none, false, true);
                        BEAST_EXPECT(!deserializeManifest(toString(st)));
                    }
                    { 
                        auto const st = buildManifestObject(
                            std::numeric_limits<std::uint32_t>::max (),
                            boost::none, false, false);
                        BEAST_EXPECT(!deserializeManifest(toString(st)));
                    }
                }
            }
        }
    }
    void testManifestDomainNames()
    {
        testcase ("Manifest Domain Names");
        auto const sk1 = generateSecretKey (KeyType::secp256k1, randomSeed());
        auto const pk1 = derivePublicKey(KeyType::secp256k1, sk1);
        auto const sk2 = generateSecretKey (KeyType::secp256k1, randomSeed());
        auto const pk2 = derivePublicKey(KeyType::secp256k1, sk2);
        auto test = [&](std::string domain)
        {
            STObject st(sfGeneric);
            st[sfSequence] = 7;
            st[sfPublicKey] = pk1;
            st[sfDomain] = makeSlice(domain);
            st[sfSigningPubKey] = pk2;
            sign(st, HashPrefix::manifest, KeyType::secp256k1, sk1, sfMasterSignature);
            sign(st, HashPrefix::manifest, KeyType::secp256k1, sk2);
            Serializer s;
            st.add(s);
            return deserializeManifest(
                std::string(static_cast<char const*> (s.data()), s.size()));
        };
        BEAST_EXPECT(test("example.com"));
        BEAST_EXPECT(test("test.example.com"));
        BEAST_EXPECT(test("example-domain.com"));
        BEAST_EXPECT(test("xn--mxavchb.gr"));
        BEAST_EXPECT(test("test.xn--mxavchb.gr"));
        BEAST_EXPECT(test("123.gr"));
        BEAST_EXPECT(test("x.yz"));
        BEAST_EXPECT(test(std::string(63, 'a') + ".example.com"));
        BEAST_EXPECT(test(std::string(63, 'a') + "." + std::string(63, 'b')));
        BEAST_EXPECT(!test("example"));
        BEAST_EXPECT(!test(".com"));
        BEAST_EXPECT(!test(".example.com"));
        BEAST_EXPECT(!test("example.com."));
        BEAST_EXPECT(!test("-example.com"));
        BEAST_EXPECT(!test("example-.com"));
        BEAST_EXPECT(!test("double..periods.example.com"));
        BEAST_EXPECT(!test("example.x"));
        BEAST_EXPECT(!test("example." + std::string(64, 'a')));
        BEAST_EXPECT(!test("example.com-org"));
        BEAST_EXPECT(!test("bang!.com"));
        BEAST_EXPECT(!test("bang!.example.com"));
        BEAST_EXPECT(!test("a.b"));
        BEAST_EXPECT(!test(std::string(64, 'a') + ".com"));
        BEAST_EXPECT(!test(std::string(64, 'a') + ".example.com"));
        BEAST_EXPECT(!test(std::string(64, 'a') + "." +  std::string(64, 'b')));
        BEAST_EXPECT(!test(std::string(64, 'a') + "." + std::string(64, 'b')));
        BEAST_EXPECT(!test(std::string(63, 'a') + "." + std::string(63, 'b') + ".example.com"));
    }
    void
    run() override
    {
        ManifestCache cache;
        {
            testcase ("apply");
            auto const sk_a = randomSecretKey();
            auto const pk_a = derivePublicKey(KeyType::ed25519, sk_a);
            auto const kp_a = randomKeyPair(KeyType::secp256k1);
            auto const s_a0 = makeManifest (
                sk_a, KeyType::ed25519, kp_a.second, KeyType::secp256k1, 0);
            auto const s_a1 = makeManifest (
                sk_a, KeyType::ed25519, kp_a.second, KeyType::secp256k1, 1);
            auto const s_aMax = makeRevocation (sk_a, KeyType::ed25519);
            auto const sk_b = randomSecretKey();
            auto const kp_b = randomKeyPair(KeyType::secp256k1);
            auto const s_b0 = makeManifest (
                sk_b, KeyType::ed25519, kp_b.second, KeyType::secp256k1, 0);
            auto const s_b1 = makeManifest (
                sk_b, KeyType::ed25519, kp_b.second, KeyType::secp256k1, 1);
            auto const s_b2 = makeManifest (
                sk_b, KeyType::ed25519, kp_b.second, KeyType::secp256k1, 2,
                true);  
            auto const fake = s_b1.serialized + '\0';
            BEAST_EXPECT(cache.applyManifest (clone (s_a0)) == ManifestDisposition::accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_a0)) == ManifestDisposition::stale);
            BEAST_EXPECT(cache.applyManifest (clone (s_a1)) == ManifestDisposition::accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_a1)) == ManifestDisposition::stale);
            BEAST_EXPECT(cache.applyManifest (clone (s_a0)) == ManifestDisposition::stale);
            BEAST_EXPECT(!cache.revoked (pk_a));
            BEAST_EXPECT(s_aMax.revoked ());
            BEAST_EXPECT(cache.applyManifest (clone (s_aMax)) == ManifestDisposition::accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_aMax)) == ManifestDisposition::stale);
            BEAST_EXPECT(cache.applyManifest (clone (s_a1)) == ManifestDisposition::stale);
            BEAST_EXPECT(cache.applyManifest (clone (s_a0)) == ManifestDisposition::stale);
            BEAST_EXPECT(cache.revoked (pk_a));
            BEAST_EXPECT(cache.applyManifest (clone (s_b0)) == ManifestDisposition::accepted);
            BEAST_EXPECT(cache.applyManifest (clone (s_b0)) == ManifestDisposition::stale);
            BEAST_EXPECT(!deserializeManifest(fake));
            BEAST_EXPECT(cache.applyManifest (clone (s_b2)) == ManifestDisposition::invalid);
        }
        testLoadStore (cache);
        testGetSignature ();
        testGetKeys ();
        testValidatorToken ();
        testManifestDeserialization ();
        testManifestDomainNames ();
        testManifestVersioning ();
    }
};
BEAST_DEFINE_TESTSUITE(Manifest,app,ripple);
} 
} 