
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/base_uint.h>
#include <ripple/basics/date.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/DeliveredAmount.h>
namespace ripple {
namespace {
bool isFull(LedgerFill const& fill)
{
    return fill.options & LedgerFill::full;
}
bool isExpanded(LedgerFill const& fill)
{
    return isFull(fill) || (fill.options & LedgerFill::expand);
}
bool isBinary(LedgerFill const& fill)
{
    return fill.options & LedgerFill::binary;
}
template <class Object>
void fillJson(Object& json, bool closed, LedgerInfo const& info, bool bFull)
{
    json[jss::parent_hash]  = to_string (info.parentHash);
    json[jss::ledger_index] = to_string (info.seq);
    json[jss::seqNum]       = to_string (info.seq);      
    if (closed)
    {
        json[jss::closed] = true;
    }
    else if (! bFull)
    {
        json[jss::closed] = false;
        return;
    }
    json[jss::ledger_hash] = to_string (info.hash);
    json[jss::transaction_hash] = to_string (info.txHash);
    json[jss::account_hash] = to_string (info.accountHash);
    json[jss::total_coins] = to_string (info.drops);
    json[jss::hash] = to_string (info.hash);
    json[jss::totalCoins] = to_string (info.drops);
    json[jss::accepted] = closed;
    json[jss::close_flags] = info.closeFlags;
    json[jss::parent_close_time] = info.parentCloseTime.time_since_epoch().count();
    json[jss::close_time] = info.closeTime.time_since_epoch().count();
    json[jss::close_time_resolution] = info.closeTimeResolution.count();
    if (info.closeTime != NetClock::time_point{})
    {
        json[jss::close_time_human] = to_string(info.closeTime);
        if (! getCloseAgree(info))
            json[jss::close_time_estimated] = true;
    }
}
template <class Object>
void fillJsonBinary(Object& json, bool closed, LedgerInfo const& info)
{
    if (! closed)
        json[jss::closed] = false;
    else
    {
        json[jss::closed] = true;
        Serializer s;
        addRaw (info, s);
        json[jss::ledger_data] = strHex (s.peekData ());
    }
}
Json::Value
fillJsonTx(
    LedgerFill const& fill,
    bool bBinary,
    bool bExpanded,
    std::shared_ptr<STTx const> const& txn,
    std::shared_ptr<STObject const> const& stMeta)
{
    if (!bExpanded)
        return to_string(txn->getTransactionID());
    Json::Value txJson{Json::objectValue};
    auto const txnType = txn->getTxnType();
    if (bBinary)
    {
        txJson[jss::tx_blob] = serializeHex(*txn);
        if (stMeta)
            txJson[jss::meta] = serializeHex(*stMeta);
    }
    else
    {
        copyFrom(txJson, txn->getJson(JsonOptions::none));
        if (stMeta)
        {
            txJson[jss::metaData] = stMeta->getJson(JsonOptions::none);
            if (txnType == ttPAYMENT || txnType == ttCHECK_CASH)
            {
                auto txMeta = std::make_shared<TxMeta>(
                    txn->getTransactionID(), fill.ledger.seq(), *stMeta);
                RPC::insertDeliveredAmount(
                    txJson[jss::metaData], fill.ledger, txn, *txMeta);
            }
        }
    }
    if ((fill.options & LedgerFill::ownerFunds) &&
        txn->getTxnType() == ttOFFER_CREATE)
    {
        auto const account = txn->getAccountID(sfAccount);
        auto const amount = txn->getFieldAmount(sfTakerGets);
        if (account != amount.getIssuer())
        {
            auto const ownerFunds = accountFunds(
                fill.ledger,
                account,
                amount,
                fhIGNORE_FREEZE,
                beast::Journal{beast::Journal::getNullSink()});
            txJson[jss::owner_funds] = ownerFunds.getText();
        }
    }
    return txJson;
}
template <class Object>
void fillJsonTx (Object& json, LedgerFill const& fill)
{
    auto&& txns = setArray (json, jss::transactions);
    auto bBinary = isBinary(fill);
    auto bExpanded = isExpanded(fill);
    try
    {
        for (auto& i: fill.ledger.txs)
        {
            txns.append(fillJsonTx(fill, bBinary, bExpanded, i.first, i.second));
        }
    }
    catch (std::exception const&)
    {
    }
}
template <class Object>
void fillJsonState(Object& json, LedgerFill const& fill)
{
    auto& ledger = fill.ledger;
    auto&& array = Json::setArray (json, jss::accountState);
    auto expanded = isExpanded(fill);
    auto binary = isBinary(fill);
    for(auto const& sle : ledger.sles)
    {
        if (fill.type == ltINVALID || sle->getType () == fill.type)
        {
            if (binary)
            {
                auto&& obj = appendObject(array);
                obj[jss::hash] = to_string(sle->key());
                obj[jss::tx_blob] = serializeHex(*sle);
            }
            else if (expanded)
                array.append(sle->getJson(JsonOptions::none));
            else
                array.append(to_string(sle->key()));
        }
    }
}
template <class Object>
void fillJsonQueue(Object& json, LedgerFill const& fill)
{
    auto&& queueData = Json::setArray(json, jss::queue_data);
    auto bBinary = isBinary(fill);
    auto bExpanded = isExpanded(fill);
    for (auto const& tx : fill.txQueue)
    {
        auto&& txJson = appendObject(queueData);
        txJson[jss::fee_level] = to_string(tx.feeLevel);
        if (tx.lastValid)
            txJson[jss::LastLedgerSequence] = *tx.lastValid;
        if (tx.consequences)
        {
            txJson[jss::fee] = to_string(
                tx.consequences->fee);
            auto spend = tx.consequences->potentialSpend +
                tx.consequences->fee;
            txJson[jss::max_spend_drops] = to_string(spend);
            auto authChanged = tx.consequences->category ==
                TxConsequences::blocker;
            txJson[jss::auth_change] = authChanged;
        }
        txJson[jss::account] = to_string(tx.account);
        txJson["retries_remaining"] = tx.retriesRemaining;
        txJson["preflight_result"] = transToken(tx.preflightResult);
        if (tx.lastResult)
            txJson["last_result"] = transToken(*tx.lastResult);
        txJson[jss::tx] = fillJsonTx(fill, bBinary, bExpanded, tx.txn, nullptr);
    }
}
template <class Object>
void fillJson (Object& json, LedgerFill const& fill)
{
    auto bFull = isFull(fill);
    if (isBinary(fill))
        fillJsonBinary(json, ! fill.ledger.open(), fill.ledger.info());
    else
        fillJson(json, ! fill.ledger.open(), fill.ledger.info(), bFull);
    if (bFull || fill.options & LedgerFill::dumpTxrp)
        fillJsonTx(json, fill);
    if (bFull || fill.options & LedgerFill::dumpState)
        fillJsonState(json, fill);
}
} 
void addJson (Json::Value& json, LedgerFill const& fill)
{
    auto&& object = Json::addObject (json, jss::ledger);
    fillJson (object, fill);
    if ((fill.options & LedgerFill::dumpQueue) && !fill.txQueue.empty())
        fillJsonQueue(json, fill);
}
Json::Value getJson (LedgerFill const& fill)
{
    Json::Value json;
    fillJson (json, fill);
    return json;
}
} 