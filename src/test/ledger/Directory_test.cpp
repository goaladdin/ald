
#include <ripple/basics/random.h>
#include <ripple/ledger/BookDirs.h>
#include <ripple/ledger/Directory.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/Protocol.h>
#include <test/jtx.h>
#include <algorithm>
namespace ripple {
namespace test {
struct Directory_test : public beast::unit_test::suite
{
    std::string
    currcode (std::size_t i)
    {
        BEAST_EXPECT (i < 17577);
        std::string code;
        for (int j = 0; j != 3; ++j)
        {
            code.push_back ('A' + (i % 26));
            i /= 26;
        }
        return code;
    }
    void
    makePages(
        Sandbox& sb,
        uint256 const& base,
        std::uint64_t n)
    {
        for (std::uint64_t i = 0; i < n; ++i)
        {
            auto p = std::make_shared<SLE>(keylet::page(base, i));
            p->setFieldV256 (sfIndexes, STVector256{});
            if (i + 1 == n)
                p->setFieldU64 (sfIndexNext, 0);
            else
                p->setFieldU64 (sfIndexNext, i + 1);
            if (i == 0)
                p->setFieldU64 (sfIndexPrevious, n - 1);
            else
                p->setFieldU64 (sfIndexPrevious, i - 1);
            sb.insert (p);
        }
    }
    void testDirectoryOrdering()
    {
        using namespace jtx;
        auto gw = Account("gw");
        auto USD = gw["USD"];
        auto alice = Account("alice");
        auto bob = Account("bob");
        {
            testcase ("Directory Ordering (without 'SortedDirectories' amendment");
            Env env(
                *this,
                supported_amendments().reset(featureSortedDirectories));
            env.fund(XRP(10000000), alice, bob, gw);
            for (std::size_t i = 1; i <= 400; ++i)
                env(offer(alice, USD(10), XRP(10)));
            {
                auto dir = Dir(*env.current(),
                    keylet::ownerDir(alice));
                std::uint32_t lastSeq = 1;
                for (auto iter = dir.begin(); iter != std::end(dir); ++iter) {
                    BEAST_EXPECT(++lastSeq == (*iter)->getFieldU32(sfSequence));
                }
                BEAST_EXPECT(lastSeq != 1);
            }
        }
        {
            testcase ("Directory Ordering (with 'SortedDirectories' amendment)");
            Env env(*this);
            env.fund(XRP(10000000), alice, gw);
            for (std::size_t i = 1; i <= 400; ++i)
                env(offer(alice, USD(i), XRP(i)));
            env.close();
            {
                auto const view = env.closed();
                std::uint64_t page = 0;
                do
                {
                    auto p = view->read(keylet::page(keylet::ownerDir(alice), page));
                    auto const& v = p->getFieldV256(sfIndexes);
                    BEAST_EXPECT (std::is_sorted(v.begin(), v.end()));
                    std::uint32_t minSeq = 2 + (page * dirNodeMaxEntries);
                    std::uint32_t maxSeq = minSeq + dirNodeMaxEntries;
                    for (auto const& e : v)
                    {
                        auto c = view->read(keylet::child(e));
                        BEAST_EXPECT(c);
                        BEAST_EXPECT(c->getFieldU32(sfSequence) >= minSeq);
                        BEAST_EXPECT(c->getFieldU32(sfSequence) < maxSeq);
                    }
                    page = p->getFieldU64(sfIndexNext);
                } while (page != 0);
            }
            auto book = BookDirs(*env.current(),
                Book({xrpIssue(), USD.issue()}));
            int count = 1;
            for (auto const& offer : book)
            {
                count++;
                BEAST_EXPECT(offer->getFieldAmount(sfTakerPays) == USD(count));
                BEAST_EXPECT(offer->getFieldAmount(sfTakerGets) == XRP(count));
            }
        }
    }
    void
    testDirIsEmpty()
    {
        testcase ("dirIsEmpty");
        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account ("charlie");
        auto const gw = Account ("gw");
        Env env(*this);
        env.fund(XRP(1000000), alice, charlie, gw);
        env.close();
        BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
        env(signers(alice, 1, { { bob, 1} }));
        env.close();
        BEAST_EXPECT(! dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
        env(signers(alice, jtx::none));
        env.close();
        BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
        std::vector<IOU> const currencies = [this, &gw]()
        {
            std::vector<IOU> c;
            c.reserve((2 * dirNodeMaxEntries) + 3);
            while (c.size() != c.capacity())
                c.push_back(gw[currcode(c.size())]);
            return c;
        }();
        {
            auto cl = currencies;
            for (auto const& c : cl)
            {
                env(trust(alice, c(50)));
                env.close();
            }
            BEAST_EXPECT(! dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
            std::shuffle (cl.begin(), cl.end(), default_prng());
            for (auto const& c : cl)
            {
                env(trust(alice, c(0)));
                env.close();
            }
            BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
        }
        {
            auto cl = currencies;
            BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
            for (auto c : currencies)
            {
                env(trust(charlie, c(50)));
                env.close();
                env(pay(gw, charlie, c(50)));
                env.close();
                env(offer(alice, c(50), XRP(50)));
                env.close();
            }
            BEAST_EXPECT(! dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
            std::shuffle (cl.begin(), cl.end(), default_prng());
            for (auto const& c : cl)
            {
                env(offer(charlie, XRP(50), c(50)));
                env.close();
            }
            BEAST_EXPECT(! dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
            std::shuffle (cl.begin(), cl.end(), default_prng());
            for (auto const& c : cl)
            {
                env(pay(alice, charlie, c(50)));
                env.close();
            }
            BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
        }
    }
    void
    testRipd1353()
    {
        testcase("RIPD-1353 Empty Offer Directories");
        using namespace jtx;
        Env env(*this);
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.trust(USD(1000), alice);
        env(pay(gw, alice, USD(1000)));
        auto const firstOfferSeq = env.seq(alice);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < dirNodeMaxEntries; ++j)
                env(offer(alice, XRP(1), USD(1)));
        env.close();
        for (auto page : {0, 2, 1})
        {
            for (int i = 0; i < dirNodeMaxEntries; ++i)
            {
                Json::Value cancelOffer;
                cancelOffer[jss::Account] = alice.human();
                cancelOffer[jss::OfferSequence] =
                    Json::UInt(firstOfferSeq + page * dirNodeMaxEntries + i);
                cancelOffer[jss::TransactionType] = jss::OfferCancel;
                env(cancelOffer);
                env.close();
            }
        }
        {
            Sandbox sb(env.closed().get(), tapNONE);
            uint256 const bookBase = getBookBase({xrpIssue(), USD.issue()});
            BEAST_EXPECT(dirIsEmpty (sb, keylet::page(bookBase)));
            BEAST_EXPECT (!sb.succ(bookBase, getQualityNext(bookBase)));
        }
        {
            env.trust(USD(0), alice);
            env(pay(alice, gw, alice["USD"](1000)));
            env.close();
            BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
        }
    }
    void
    testEmptyChain()
    {
        testcase("Empty Chain on Delete");
        using namespace jtx;
        Env env(*this);
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice);
        env.close();
        uint256 base;
        base.SetHex("fb71c9aa3310141da4b01d6c744a98286af2d72ab5448d5adc0910ca0c910880");
        uint256 item;
        item.SetHex("bad0f021aa3b2f6754a8fe82a5779730aa0bbbab82f17201ef24900efc2c7312");
        {
            Sandbox sb(env.closed().get(), tapNONE);
            makePages (sb, base, 3);
            {
                auto p = sb.peek (keylet::page(base, 1));
                BEAST_EXPECT(p);
                STVector256 v;
                v.push_back (item);
                p->setFieldV256 (sfIndexes, v);
                sb.update(p);
            }
            BEAST_EXPECT (sb.dirRemove (keylet::page(base, 0), 1, keylet::unchecked(item), false));
            BEAST_EXPECT (!sb.peek(keylet::page(base, 2)));
            BEAST_EXPECT (!sb.peek(keylet::page(base, 1)));
            BEAST_EXPECT (!sb.peek(keylet::page(base, 0)));
        }
        {
            Sandbox sb(env.closed().get(), tapNONE);
            makePages (sb, base, 4);
            {
                auto p1 = sb.peek (keylet::page(base, 1));
                BEAST_EXPECT(p1);
                STVector256 v1;
                v1.push_back (~item);
                p1->setFieldV256 (sfIndexes, v1);
                sb.update(p1);
                auto p2 = sb.peek (keylet::page(base, 2));
                BEAST_EXPECT(p2);
                STVector256 v2;
                v2.push_back (item);
                p2->setFieldV256 (sfIndexes, v2);
                sb.update(p2);
            }
            BEAST_EXPECT (sb.dirRemove (keylet::page(base, 0), 2, keylet::unchecked(item), false));
            BEAST_EXPECT (!sb.peek(keylet::page(base, 3)));
            BEAST_EXPECT (!sb.peek(keylet::page(base, 2)));
            auto p1 = sb.peek(keylet::page(base, 1));
            BEAST_EXPECT (p1);
            BEAST_EXPECT (p1->getFieldU64 (sfIndexNext) == 0);
            BEAST_EXPECT (p1->getFieldU64 (sfIndexPrevious) == 0);
            auto p0 = sb.peek(keylet::page(base, 0));
            BEAST_EXPECT (p0);
            BEAST_EXPECT (p0->getFieldU64 (sfIndexNext) == 1);
            BEAST_EXPECT (p0->getFieldU64 (sfIndexPrevious) == 1);
        }
    }
    void run() override
    {
        testDirectoryOrdering();
        testDirIsEmpty();
        testRipd1353();
        testEmptyChain();
    }
};
BEAST_DEFINE_TESTSUITE_PRIO(Directory,ledger,ripple,1);
}
}