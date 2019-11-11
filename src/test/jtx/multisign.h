#ifndef RIPPLE_TEST_JTX_MULTISIGN_H_INCLUDED
#define RIPPLE_TEST_JTX_MULTISIGN_H_INCLUDED
#include <test/jtx/Account.h>
#include <test/jtx/amount.h>
#include <test/jtx/owners.h>
#include <test/jtx/tags.h>
#include <cstdint>
namespace ripple {
namespace test {
namespace jtx {
struct signer
{
    std::uint32_t weight;
    Account account;
    signer (Account account_,
            std::uint32_t weight_ = 1)
        : weight(weight_)
        , account(std::move(account_))
    {
    }
};
Json::Value
signers (Account const& account,
    std::uint32_t quorum,
        std::vector<signer> const& v);
Json::Value
signers (Account const& account, none_t);
class msig
{
public:
    struct Reg
    {
        Account acct;
        Account sig;
        Reg (Account const& masterSig)
        : acct (masterSig)
        , sig (masterSig)
        { }
        Reg (Account const& acct_, Account const& regularSig)
        : acct (acct_)
        , sig (regularSig)
        { }
        Reg (char const* masterSig)
        : acct (masterSig)
        , sig (masterSig)
        { }
        Reg (char const* acct_, char const* regularSig)
        : acct (acct_)
        , sig (regularSig)
        { }
        bool operator< (Reg const& rhs) const
        {
            return acct < rhs.acct;
        }
    };
    std::vector<Reg> signers;
public:
    msig (std::vector<Reg> signers_);
    template <class AccountType, class... Accounts>
    msig (AccountType&& a0, Accounts&&... aN)
        : msig(make_vector(
            std::forward<AccountType>(a0),
                std::forward<Accounts>(aN)...))
    {
    }
    void
    operator()(Env&, JTx& jt) const;
private:
    template <class AccountType>
    static
    void
    helper (std::vector<Reg>& v,
        AccountType&& account)
    {
        v.emplace_back(std::forward<
            Reg>(account));
    }
    template <class AccountType, class... Accounts>
    static
    void
    helper (std::vector<Reg>& v,
        AccountType&& a0, Accounts&&... aN)
    {
        helper(v, std::forward<AccountType>(a0));
        helper(v, std::forward<Accounts>(aN)...);
    }
    template <class... Accounts>
    static
    std::vector<Reg>
    make_vector(Accounts&&... signers)
    {
        std::vector<Reg> v;
        v.reserve(sizeof...(signers));
        helper(v, std::forward<
            Accounts>(signers)...);
        return v;
    }
};
using siglists = owner_count<ltSIGNER_LIST>;
} 
} 
} 
#endif
