#include <chrono>
#include <cstdio>
#include <thread>

#include <dataObject/ConvertFile.h>
#include <retesteth/ethObjects/common.h>
#include <retesteth/session/ToolImpl.h>

// DataObject modifiers
void mod_valuesToLowerCase(DataObject& _obj)
{
    if (_obj.type() == DataType::String)
    {
        string value = _obj.asString();
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return std::tolower(c); });
        _obj = value;
    }
}

// Remove leading zeros from hex values leaving 0x0004 - > 0x4
void mod_removeLeadingZerosFromHexValues(DataObject& _obj)
{
    static std::vector<std::string> const c_hashes{std::string{"to"}, std::string{"data"}};
    if (_obj.type() == DataType::String && !inArray(c_hashes, _obj.getKey()))
    {
        string const& origVal = _obj.asString();
        bool replacePossible = true;
        while (replacePossible)
        {
            if (origVal[0] == '0' && origVal[1] == 'x' && origVal[2] == '0' && origVal.size() >= 4)
            {
                _obj.setString("0x" + origVal.substr(3));
                continue;
            }
            replacePossible = false;
        }
    }
}

// Remove leading zeros from hex values leaving 0x0004 - > 0x04
void mod_removeLeadingZerosFromHexValuesEVEN(DataObject& _obj)
{
    mod_removeLeadingZerosFromHexValues(_obj);
    static std::vector<std::string> const c_hashes{std::string{"to"}, std::string{"data"}};
    if (_obj.type() == DataType::String && !inArray(c_hashes, _obj.getKey()))
    {
        object::DigitsType t = object::stringIntegerType(_obj.asString());
        if (t == object::DigitsType::UnEvenHexPrefixed)
            _obj.setString("0x0" + _obj.asString().substr(2));
    }
}


std::string ToolImpl::web3_clientVersion()
{
    rpcCall("", {});
    ETH_FAIL_MESSAGE("Request: web3_clientVersion");
    return "";
}

// ETH Methods
// perhaps take raw rlp instead ??
std::string ToolImpl::eth_sendRawTransaction(scheme_transaction const& _transaction)
{
    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: eth_sendRawTransaction \n" + _transaction.getData().asJson());
    m_transactions.push_back(_transaction);
    ETH_TEST_MESSAGE("Response: " + _transaction.getHash());
    return _transaction.getHash();
}

int ToolImpl::eth_getTransactionCount(std::string const& _address, std::string const& _blockNumber)
{
    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: eth_getTransactionCount " + _blockNumber + " " + _address);

    ToolBlock const& block = getBlockByHashOrNumber(_blockNumber);
    if (block.getPostState().count(_address) && block.getPostState().atKey(_address).count("nonce"))
    {
        string const& nonce = block.getPostState().atKey(_address).atKey("nonce").asString();
        ETH_TEST_MESSAGE("Response: " + nonce);
        return test::hexOrDecStringToInt(nonce);
    }
    ETH_TEST_MESSAGE("0");
    return 0;
}

std::string ToolImpl::eth_blockNumber()
{
    rpcCall("", {});
    string blNumber = "0x00";
    ETH_TEST_MESSAGE("Request: eth_blockNumber");
    if (getCurrChain().size() > 0)
        blNumber = dev::toCompactHexPrefixed(getCurrChain().size(), 1);
    ETH_TEST_MESSAGE("Response: eth_blockNumber {" + blNumber + "}");
    return blNumber;
}

test::scheme_RPCBlock ToolImpl::eth_getBlockByHash(string const& _hash, bool _fullObjects)
{
    rpcCall("", {});
    (void)_fullObjects;  // always full objects
    ETH_TEST_MESSAGE("Request: eth_getBlockByHash `" + _hash + "`");
    for (auto const& chain : m_blockchainMap)
    {
        ToolChain const& tch = chain.second;
        for (auto const& bl : tch)
        {
            if (bl.getRPCResponse().getBlockHeader().atKey("hash") == _hash)
            {
                ETH_TEST_MESSAGE(
                    "Response: eth_getBlockByHash " + bl.getRPCResponse().getData().asJson());
                return bl.getRPCResponse();
            }
        }
    }

    ETH_TEST_MESSAGE("Response: eth_getBlockByHash (hash not found: " + _hash + ")");
    return scheme_RPCBlock(DataObject());
}

ToolImpl::ToolBlock const& ToolImpl::getBlockByHashOrNumber(string const& _hashOrNumber) const
{
    if (_hashOrNumber.size() == 66)
    {
        // need to look up all chains !!!
        for (size_t i = 0; i < getCurrChain().size(); i++)
            if (getCurrChain().at(i).getHash() == _hashOrNumber)
                return getCurrChain().at(i);
    }
    else
    {
        int bNumber = hexOrDecStringToInt(_hashOrNumber);
        ETH_ERROR_REQUIRE_MESSAGE(
            bNumber >= 0 && bNumber < 10000, "getBlockByHashOrNumber sanitize block number!");
        if (bNumber == 0)
            return getGenesis();
        bNumber--;  // chain vector starts with 0
        if (bNumber >= 0 && (size_t)bNumber < getCurrChain().size())
            return getCurrChain().at(bNumber);
    }
    ETH_ERROR_MESSAGE("Wrong block hash or number! " + _hashOrNumber);
    return getCurrChain().at(0);
}

// Make an RPC like response using data that we know from tool
scheme_RPCBlock ToolImpl::internalConstructResponseGetBlockByHashOrNumber(
    DataObject const& _toolResponse)
{
    DataObject constructResponse(DataType::Null);
    scheme_toolResponse toolResponse(_toolResponse);
    DataObject const& rdata = toolResponse.getData();
    DataObject const& currentHeader = m_currentBlockHeader.header;
    size_t receiptSize = rdata.atKey("receipts").getSubObjects().size();

    DataObject lastReceipt;
    if (receiptSize > 0)
    {
        DataObject const& _lastReceipt =
            rdata.atKey("receipts")
                .getSubObjects()
                .at(rdata.atKey("receipts").getSubObjects().size() - 1);
        lastReceipt["logsBloom"] = _lastReceipt.atKey("logsBloom");

        // Construct fake blockheader from all the info we know
        // Env of block execution from test
        // TooResponse for state hashes and transactions

        // Get all gas usage provided by tool
        dev::u256 maxCommGasUsed = 0;
        for (auto const& receipt : rdata.atKey("receipts").getSubObjects())
        {
            // Find transaction that was sent
            scheme_transaction const* trScheme = 0;
            for (auto const& tr : m_transactions)
            {
                if (tr.getHash() == receipt.atKey("transactionHash").asString())
                    trScheme = &tr;
            }
            ETH_ERROR_REQUIRE_MESSAGE(trScheme != 0, "Not found transaction in m_transactions");

            // Fill full transaction object from all the info we know (rpc style)
            // m_transactions is raw info about transactions that was requested to be setn
            // receipts are the valid transaction that were executed by tool
            DataObject fullTransaction;
            fullTransaction["blockHash"] =
                dev::toHexPrefixed(h256(0));  // update when we know the hash
            fullTransaction["blockNumber"] =
                dev::toCompactHexPrefixed(m_currentBlockHeader.currentBlockNumber, 1);
            fullTransaction["from"] = "0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b";  // recover vrs
            fullTransaction["gas"] = trScheme->getData().atKey("gasLimit").asString();
            fullTransaction["gasPrice"] = trScheme->getData().atKey("gasPrice").asString();
            fullTransaction["hash"] = receipt.atKey("transactionHash").asString();
            fullTransaction["input"] = trScheme->getData().atKey("data").asString();
            fullTransaction["nonce"] = trScheme->getData().atKey("nonce").asString();
            fullTransaction["to"] = trScheme->getData().atKey("to").asString();
            fullTransaction["transactionIndex"] = receipt.atKey("transactionIndex").asString();
            fullTransaction["value"] = trScheme->getData().atKey("value").asString();
            if (trScheme->getData().count("v"))
            {
                fullTransaction["v"] = trScheme->getData().atKey("v").asString();
                fullTransaction["r"] = trScheme->getData().atKey("r").asString();
                fullTransaction["s"] = trScheme->getData().atKey("s").asString();
            }
            else
            {
                fullTransaction["v"] = "0x00";
                fullTransaction["r"] = "0x00";
                fullTransaction["s"] = "0x00";
            }
            constructResponse["transactions"].addArrayObject(fullTransaction);

            u256 commGasUsed = u256(receipt.atKey("cumulativeGasUsed").asString());
            maxCommGasUsed = (commGasUsed > maxCommGasUsed) ? commGasUsed : maxCommGasUsed;
        }
        lastReceipt["maxCommGasUsed"] = dev::toCompactHexPrefixed(maxCommGasUsed, 1);
    }
    else
    {
        // Default values. (the genesis block contain no receipts) so use defaults
        lastReceipt["logsBloom"] =
            "0x000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
        lastReceipt["maxCommGasUsed"] = "0x00";
        constructResponse["transactions"] = DataObject(DataType::Array);
    }

    constructResponse["extraData"] = currentHeader.atKey("extraData");
    constructResponse["gasLimit"] = currentHeader.atKey("gasLimit");
    constructResponse["gasUsed"] = lastReceipt.atKey("maxCommGasUsed").asString();
    constructResponse["logsBloom"] = lastReceipt.atKey("logsBloom");
    constructResponse["miner"] = currentHeader.atKey("author");
    constructResponse["number"] =
        dev::toCompactHexPrefixed(m_currentBlockHeader.currentBlockNumber, 1);
    constructResponse["receiptsRoot"] = rdata.atKey("receiptRoot");
    constructResponse["stateRoot"] = rdata.atKey("stateRoot");
    constructResponse["timestamp"] = dev::toCompactHexPrefixed(m_currentBlockHeader.timestamp, 1);
    constructResponse["uncles"] = DataObject(DataType::Array);
    for (auto const& un : m_currentBlockHeader.uncles)
        constructResponse["uncles"].addArrayObject(un.getBlockHash());
    constructResponse["transactionsRoot"] = rdata.atKey("txRoot");
    constructResponse["difficulty"] = currentHeader.atKey("difficulty");

    constructResponse["hash"] =
        dev::toHexPrefixed(h256(0));  // Recalculate after with overwriteBlockHeader
    constructResponse["sha3Uncles"] =
        dev::toHexPrefixed(h256(0));  // Recalculate after with overwriteBlockHeader
    constructResponse["parentHash"] = m_currentBlockHeader.header.atKey("parentHash");
    constructResponse["size"] = "0x00";  // Not Needed";
    constructResponse.performModifier(mod_valuesToLowerCase);

    scheme_RPCBlock rpcBlock(constructResponse);
    for (auto const& un : m_currentBlockHeader.uncles)
        rpcBlock.addUncle(un);
    rpcBlock.recalculateUncleHash();
    rpcBlock.overwriteBlockHeader(rpcBlock.getBlockHeader());
    rpcBlock.setLogsHash(rdata.atKey("logsHash").asString());

    // Update transaction info in block rpc response
    rpcBlock.tool_updateTransactionInfo();

    return rpcBlock;
}

test::scheme_RPCBlock ToolImpl::eth_getBlockByNumber(
    BlockNumber const& _blockNumber, bool _fullObjects)
{
    rpcCall("", {});
    (void)_fullObjects;
    ETH_TEST_MESSAGE("Request: eth_getBlockByNumber " + _blockNumber.getBlockNumberAsString());
    int bNumber = _blockNumber.getBlockNumberAsInt();
    if (bNumber > 0 && (size_t)bNumber <= getCurrChain().size())
    {
        ETH_TEST_MESSAGE("Response: eth_getBlockByNumber " +
                         getCurrChain().at(bNumber - 1).getRPCResponse().getData().asJson());
        return getCurrChain().at(bNumber - 1).getRPCResponse();
    }
    else if (bNumber == 0)
        return getGenesis().getRPCResponse();
    return scheme_RPCBlock(DataObject());
}

std::string ToolImpl::eth_getCode(std::string const& _address, std::string const& _blockNumber)
{
    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: eth_getCode " + _blockNumber + " " + _address);

    ToolBlock const& block = getBlockByHashOrNumber(_blockNumber);
    if (block.getPostState().count(_address) && block.getPostState().atKey(_address).count("code"))
    {
        string const& code = block.getPostState().atKey(_address).atKey("code").asString();
        ETH_TEST_MESSAGE(code);
        return code;
    }
    ETH_TEST_MESSAGE("0x");
    return "0x";
}

std::string ToolImpl::eth_getBalance(std::string const& _address, std::string const& _blockNumber)
{
    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: eth_getBalance " + _blockNumber + " " + _address);

    ToolBlock const& block = getBlockByHashOrNumber(_blockNumber);
    if (block.getPostState().count(_address) &&
        block.getPostState().atKey(_address).count("balance"))
    {
        string const& balance = block.getPostState().atKey(_address).atKey("balance").asString();
        ETH_TEST_MESSAGE(balance);
        return balance;
    }
    ETH_TEST_MESSAGE("0x");
    return "0x";
}

// Debug
scheme_debugAccountRange ToolImpl::debug_accountRange(std::string const& _blockHashOrNumber,
    int _txIndex, std::string const& _address, int _maxResults)
{
    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: debug_accountRange " + _blockHashOrNumber);
    (void)_txIndex;
    (void)_address;

    int iAcc = 0;
    DataObject constructResponse;
    ToolBlock const& block = getBlockByHashOrNumber(_blockHashOrNumber);
    for (auto const& el : block.getPostState().getSubObjects())
    {
        constructResponse["addressMap"].addSubObject(toString(iAcc++), DataObject(el.getKey()));
        if (iAcc == _maxResults)
            break;
    }
    constructResponse["nextKey"] =
        "0x0000000000000000000000000000000000000000000000000000000000000000";
    ETH_TEST_MESSAGE(constructResponse.asJson());
    return scheme_debugAccountRange(constructResponse);
}


DataObject ToolImpl::debug_storageRangeAt(std::string const& _blockHashOrNumber, int _txIndex,
    std::string const& _address, std::string const& _begin, int _maxResults)
{
    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: debug_storageRangeAt " + _blockHashOrNumber + " " + _address);
    (void)_txIndex;
    (void)_begin;

    DataObject constructResponse;
    ToolBlock const& block = getBlockByHashOrNumber(_blockHashOrNumber);
    if (block.getPostState().count(_address))
    {
        constructResponse["complete"] = true;
        if (block.getPostState().atKey(_address).count("storage"))
        {
            int iStore = 0;
            for (auto const& el :
                block.getPostState().atKey(_address).atKey("storage").getSubObjects())
            {
                DataObject record;
                record["key"] = el.getKey();
                record["value"] = el.asString();
                record.performModifier(mod_removeLeadingZerosFromHexValuesEVEN);
                constructResponse["storage"][toString(iStore)] = record;
                if (iStore++ == _maxResults)
                    break;
            }
        }
        else
            constructResponse["storage"] = DataObject();
    }
    ETH_TEST_MESSAGE(constructResponse.asJson());
    return constructResponse;
}

scheme_debugTraceTransaction ToolImpl::debug_traceTransaction(std::string const& _trHash)
{
    rpcCall("", {});
    ETH_FAIL_MESSAGE("Request: debug_traceTransaction");
    return scheme_debugTraceTransaction(DataObject(_trHash));
}

// Test
void ToolImpl::test_setChainParams(DataObject const& _config)
{
    m_chainGenesis.clear();
    m_chainParams.clear();
    m_transactions.clear();
    m_currentBlockHeader.reset();
    m_blockchainMap.clear();
    m_current_chain_ind = 0;
    m_maxChainID = 0;

    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: test_setChainParams \n" + _config.asJson());
    m_chainParams = _config;
    m_chainParams.performModifier(mod_removeLeadingZerosFromHexValuesEVEN);

    DataObject genesisHeader;
    DataObject const& env = m_chainParams.atKey("genesis");

    // Values from ENV section
    genesisHeader["coinbase"] = env.atKey("author");
    genesisHeader["difficulty"] = env.atKey("difficulty");
    genesisHeader["gasLimit"] = env.atKey("gasLimit");
    genesisHeader["extraData"] = env.atKey("extraData");
    genesisHeader["timestamp"] = env.atKey("timestamp");

    // Default Genesis Values
    genesisHeader["bloom"] =
        "0x0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000";
    genesisHeader["gasUsed"] = "0x00";
    genesisHeader["number"] = "0x00";
    genesisHeader["parentHash"] =
        "0x0000000000000000000000000000000000000000000000000000000000000000";
    genesisHeader["receiptTrie"] =
        "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421";
    genesisHeader["transactionsTrie"] =
        "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421";
    genesisHeader["uncleHash"] =
        "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347";

    // Values need to calculate
    genesisHeader["hash"] = "0x0000000000000000000000000000000000000000000000000000000000000000";
    genesisHeader["stateRoot"] =
        "0x0000000000000000000000000000000000000000000000000000000000000000";

    // PrepareGenesis hashes using tool
    m_currentBlockHeader.isMiningGenesis = true;
    m_currentBlockHeader.currentBlockNumber = 0;
    m_currentBlockHeader.header["author"] = env.atKey("author");
    m_currentBlockHeader.header["difficulty"] = env.atKey("difficulty");
    m_currentBlockHeader.header["gasLimit"] = env.atKey("gasLimit");
    m_currentBlockHeader.header["extraData"] = env.atKey("extraData");
    m_currentBlockHeader.header["parentHash"] =
        "0x0000000000000000000000000000000000000000000000000000000000000000";

    size_t const cchID = m_current_chain_ind;
    size_t const beginSize = m_blockchainMap[m_current_chain_ind].size();
    ETH_ERROR_REQUIRE_MESSAGE(beginSize == 0, "test_setChain params currChain size must be 0");
    test_mineBlocks(1);  // use tool to calculate the hash of genesis pre state (must work, fail on
                         // error)
    ETH_ERROR_REQUIRE_MESSAGE(
        cchID == m_current_chain_ind, "Genesis block must be mined on the same ch id");
    ETH_ERROR_REQUIRE_MESSAGE(m_blockchainMap.at(m_current_chain_ind).size() == beginSize + 1,
        "test_mineBlock must mine a block in test_setChainParams");
    m_currentBlockHeader.isMiningGenesis = false;

    // Now we know the genesis stateRoot from the tool
    genesisHeader["stateRoot"] =
        getLastBlock().getRPCResponse().getBlockHeader().atKey("stateRoot");

    ToolBlock genesis = getLastBlock();
    genesis.overwriteBlockHeader(genesisHeader);
    m_chainGenesis.push_back(genesis);
    // remove fake block which is actually genesis
    m_blockchainMap.at(m_current_chain_ind).pop_back();

    ETH_TEST_MESSAGE("Response test_setChainParams: {true}");
    ETH_TEST_MESSAGE(genesis.getRPCResponse().getBlockHeader().asJson());
}

void ToolImpl::test_rewindToBlock(size_t _blockNr)
{
    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: test_rewindToBlock");
    if (_blockNr == 0)
    {
        m_blockchainMap.clear();
        m_blockchainMap[m_current_chain_ind].size();  // initialize defualt chain
    }
    else
    {
        size_t const cchSize = m_blockchainMap.at(m_current_chain_ind).size();
        for (size_t i = _blockNr; i < cchSize; i++)
            m_blockchainMap.at(m_current_chain_ind).pop_back();
    }

    m_currentBlockHeader.reset();

    scheme_RPCBlock const& currentBlock = getLastBlock().getRPCResponse();
    DataObject const& env = currentBlock.getBlockHeader();
    m_currentBlockHeader.timestamp = test::hexOrDecStringToInt(env.atKey("timestamp").asString());
    m_currentBlockHeader.currentBlockNumber = test::hexOrDecStringToInt(currentBlock.getNumber());
    m_currentBlockHeader.header["author"] = env.atKey("coinbase");
    m_currentBlockHeader.header["difficulty"] = env.atKey("difficulty");
    m_currentBlockHeader.header["gasLimit"] = env.atKey("gasLimit");
    m_currentBlockHeader.header["extraData"] = env.atKey("extraData");
    if (_blockNr == 0)
        m_currentBlockHeader.header["parentHash"] =
            "0x0000000000000000000000000000000000000000000000000000000000000000";
    else if (_blockNr == 1)
        m_currentBlockHeader.header["parentHash"] = getGenesis().getHash();
    else
        m_currentBlockHeader.header["parentHash"] = getLastBlock(2).getHash();
    m_currentBlockHeader.header.performVerifier(test::ver_checkHash32Fields);
}

void ToolImpl::test_modifyTimestamp(unsigned long long _timestamp)
{
    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: test_modifyTimestamp " + DataObject(_timestamp).asJson());
    m_currentBlockHeader.timestamp = _timestamp;
}

string ToolImpl::prepareAllocForTool() const
{
    if (m_currentBlockHeader.currentBlockNumber > 1)
        return getLastBlock().getPostState().asJsonNoFirstKey();
    else
        return m_chainParams.atKey("accounts").asJsonNoFirstKey();
}

string ToolImpl::prepareEnvForTool() const
{
    DataObject env;
    env["currentCoinbase"] = m_currentBlockHeader.header.atKey("author");
    env["currentDifficulty"] = m_currentBlockHeader.header.atKey("difficulty");
    env["currentGasLimit"] = m_currentBlockHeader.header.atKey("gasLimit");
    env["currentNumber"] = dev::toCompactHexPrefixed(m_currentBlockHeader.currentBlockNumber, 1);
    env["currentTimestamp"] = dev::toCompactHexPrefixed(m_currentBlockHeader.timestamp, 1);
    env["previousHash"] = m_currentBlockHeader.header.atKey("parentHash");

    if (!m_currentBlockHeader.isMiningGenesis)
    {
        int k = 0;
        env["blockHashes"][toString(k++)] = getGenesis().getHash();
        for (auto const& bl : getCurrChain())
            env["blockHashes"][toString(k++)] = bl.getHash();

        for (auto const& un : m_currentBlockHeader.uncles)
        {
            DataObject uncle;
            uncle["delta"] =
                m_currentBlockHeader.currentBlockNumber - test::hexOrDecStringToInt(un.getNumber());
            uncle["address"] = un.getBlockHeader().atKey("coinbase");
            env["ommers"].addArrayObject(uncle);
        }
    }

    return env.asJson();
}

string ToolImpl::prepareTxsForTool() const
{
    DataObject txs(DataType::Array);
    for (auto const& tx : m_transactions)
    {
        DataObject txToolFormat = tx.getDataForBCTest();
        txToolFormat.performModifier(mod_removeLeadingZerosFromHexValues);
        txToolFormat.renameKey("data", "input");
        txToolFormat.renameKey("gasLimit", "gas");
        if (txToolFormat.atKey("value").asString() == "0x" ||
            txToolFormat.atKey("value").asString().empty())
            txToolFormat.atKeyUnsafe("value") = "0x0";
        if (txToolFormat.atKey("to").asString().empty() ||
            txToolFormat.atKey("to").asString() == "0x")
            txToolFormat.removeKey("to");
        txToolFormat["hash"] = tx.getHash();
        txs.addArrayObject(txToolFormat);
    }
    return txs.asJson();
}

string ToolImpl::test_mineBlocks(int _number, bool _canFail)
{
    (void)_canFail;
    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: test_mineBlocks");
    ETH_ERROR_REQUIRE_MESSAGE(_number == 1, "Make sure test_mineBlocks mine 1 block");

    // env.json file
    // if mining a new block (not from import RawBlock)
    if (!m_currentBlockHeader.isImportRawBlock && !m_currentBlockHeader.isMiningGenesis)
    {
        // Information about the block that currently mined
        // if m_blockchain.size == 0 then we mine [1] first block
        // if m_blockchain.size == 1 then we mine [2] second block
        m_currentBlockHeader.currentBlockNumber = getCurrChain().size() + 1;  // future block
        size_t const _blockNr = m_currentBlockHeader.currentBlockNumber;
        string const* pHash;
        if (_blockNr == 1)  // if we are mining blockN = 1
            pHash = &getGenesis().getHash();
        else
            pHash = &getLastBlock().getHash();
        m_currentBlockHeader.header["parentHash"] = *pHash;
    }
    fs::path envPath = m_tmpDir / "env.json";
    writeFile(envPath.string(), prepareEnvForTool());

    // txs.json file
    fs::path txsPath = m_tmpDir / "txs.json";
    writeFile(txsPath.string(), prepareTxsForTool());

    // alloc.json file
    fs::path allocPath = m_tmpDir / "alloc.json";
    writeFile(allocPath.string(), prepareAllocForTool());

    // output file
    fs::path outPath = m_tmpDir / "out.json";
    fs::path outAllocPath = m_tmpDir / "outAlloc.json";

    string cmd = string(m_toolPath);
    cmd += " --input.alloc " + allocPath.string();
    cmd += " --input.txs " + txsPath.string();
    cmd += " --input.env " + envPath.string();
    cmd += " --output.result " + outPath.string();
    cmd += " --output.alloc " + outAllocPath.string();
    cmd += " --state.fork " + m_chainParams.atKey("params").atKey("fork").asString();

    if (!m_currentBlockHeader.isMiningGenesis)  // If calculating geneis block disable rewards. so
                                                // to see the state root hash
        if (m_chainParams.atKey("sealEngine").asString() == "NoProof")
        {
            // Setup mining rewards
            DataObject const& rewards =
                Options::get().getDynamicOptions().getCurrentConfig().getMiningRewardInfo();
            string const& reward =
                rewards.atKey(m_chainParams.atKey("params").atKey("fork").asString()).asString();
            cmd += " --state.reward " + reward;
        }

    ETH_TEST_MESSAGE("Alloc:\n" + contentsString(allocPath.string()));
    if (m_transactions.size())
        ETH_TEST_MESSAGE("Txs:\n" + contentsString(txsPath.string()));
    ETH_TEST_MESSAGE("Env:\n" + contentsString(envPath.string()));
    test::executeCmd(cmd, false);
    ETH_TEST_MESSAGE("Res:\n" + contentsString(outPath.string()));
    ETH_TEST_MESSAGE("RAlloc:\n" + contentsString(outAllocPath.string()));

    // Construct block rpc response
    DataObject const toolResponse = ConvertJsoncppStringToData(contentsString(outPath));
    scheme_RPCBlock blockRPC = internalConstructResponseGetBlockByHashOrNumber(toolResponse);

    ToolBlock block(blockRPC, m_chainParams,                        // Env, alloc info
        ConvertJsoncppStringToData(contentsString(outAllocPath)));  // Result state
    if (toolResponse.count("rejected"))
        block.markInvalidTransactions();

    m_transactions.clear();  // comment to trigger transaction rejection

    if (!m_currentBlockHeader.isMiningGenesis)
        m_currentBlockHeader.reset();

    // TODO:: verify tx from our list and from tool response

    m_blockchainMap[m_current_chain_ind].push_back(block);
    fs::remove_all(m_tmpDir);
    ETH_TEST_MESSAGE("Response test_mineBlocks {" + toString(getCurrChain().size()) + "}");
    ETH_TEST_MESSAGE(blockRPC.getData().asJson());
    return toString(getCurrChain().size());
}

string rlpToString(dev::RLP const& _rlp, bool _corretHexOdd = false)
{
    std::ostringstream stream;
    stream << _rlp.toBytes();
    if (stream.str() == "0x")
        return (_corretHexOdd) ? "0x00" : "0x0";
    return stream.str();
}

void ToolImpl::doChainReorg()
{
    // Select highest total difficulty chain
    dev::u256 maxTotalDiff = 0;
    for (auto const& chain : m_blockchainMap)
    {
        dev::u256 tch_d = 0;
        ToolChain const& tch = chain.second;
        for (auto const& bl : tch)
            tch_d += u256(bl.getRPCResponse().getBlockHeader().atKey("difficulty").asString());

        if (tch_d > maxTotalDiff)
        {
            m_current_chain_ind = chain.first;
            maxTotalDiff = tch_d;
        }
    }
}

void ToolImpl::makeForkForBlockWithPHash(string const& _parentHash)
{
    // Look for _parentHash in chain database
    // if hash is found make that chain current
    // if hash is below top head of found chain, make a new chain and make that chain current
    bool found = false;
    if (getGenesis().getHash() == _parentHash)
    {
        // Parent is genesis, make new fork starting from genesis
        m_current_chain_ind = ++m_maxChainID;
        m_blockchainMap[m_current_chain_ind].size();
        return;
    }

    for (auto const& chain : m_blockchainMap)
    {
        ToolChain const& tch = chain.second;
        for (auto const& bl : tch)
        {
            if (bl.getHash() == _parentHash)
            {
                found = true;

                // if it is last block in chain
                if (tch.size() == (size_t)bl.getNumber())
                    m_current_chain_ind = chain.first;
                else
                {
                    // make a new fork
                    m_current_chain_ind = ++m_maxChainID;
                    ETH_ERROR_REQUIRE_MESSAGE((size_t)bl.getNumber() <= tch.size(),
                        "bl in chain number must be <= chain size");
                    // Copy up to fork block
                    for (size_t i = 0; i < (size_t)bl.getNumber(); i++)
                        m_blockchainMap[m_current_chain_ind].push_back(tch.at(i));
                }
                break;
            }
        }
        if (found)
            break;
    }

    if (!found)
        ETH_ERROR_MESSAGE("ParentHash not found: " + _parentHash);
}

// Import block from RAW rlp and validate it according to ethereum rules
// Very logic heavy function. Must be on the client side. Its a clien logic.
string ToolImpl::test_importRawBlock(std::string const& _blockRLP)
{
    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: test_importRawBlock, following transaction import are internal");
    bytes blockBytes = dev::fromHex(_blockRLP);

    string rawBlockHash = "";
    size_t rawImportedBlockNumber = 100000;
    DataObject rawImportedBlockParentHash("");

    try
    {
        dev::RLP blockRLP(blockBytes);
        if (!blockRLP.isList())
            throw dev::RLPException("Block RLP is expected to be list");
        dev::RLP header = blockRLP[0];  // block header
        if (!header.isList())
            throw dev::RLPException("RLP blockHeader is expected to be list");

        // 0 - parentHash           // 8 - number
        // 1 - uncleHash            // 9 - gasLimit
        // 2 - coinbase             // 10 - gasUsed
        // 3 - stateRoot            // 11 - timestamp
        // 4 - transactionTrie      // 12 - extraData
        // 5 - receiptTrie          // 13 - mixHash
        // 6 - bloom                // 14 - nonce
        // 7 - difficulty
        rawBlockHash = dev::toHexPrefixed(dev::sha3(header.data()));
        rawImportedBlockNumber = test::hexOrDecStringToInt(rlpToString(header[8]));
        rawImportedBlockParentHash = rlpToString(header[0]);

        // Check if we know parent hash and do chain reorg
        string const pHash = rlpToString(header[0]);
        string const bNumber = rlpToString(header[8]);

        ETH_TEST_MESSAGE("Attempt to import number `" + toString(bNumber) +
                         "` on top of chain size `" + toString(getCurrChain().size()));
        ETH_TEST_MESSAGE("ParentHash: " + pHash);
        if (!object::validateHash32(pHash))
            throw dev::RLPException("Parent has is not hash32");
        makeForkForBlockWithPHash(pHash);

        // Information about current blockheader from ToolImpl prespective
        m_currentBlockHeader.reset();
        m_currentBlockHeader.header["author"] = rlpToString(header[2]);
        m_currentBlockHeader.header["difficulty"] = rlpToString(header[7]);
        m_currentBlockHeader.header["gasLimit"] = rlpToString(header[9]);
        m_currentBlockHeader.header["extraData"] = rlpToString(header[12]);
        m_currentBlockHeader.header["timestamp"] = rlpToString(header[11]);
        m_currentBlockHeader.timestamp = test::hexOrDecStringToInt(rlpToString(header[11]));
        m_currentBlockHeader.header["nonce"] = rlpToString(header[14]);
        m_currentBlockHeader.header["mixHash"] = rlpToString(header[13]);
        m_currentBlockHeader.header.performModifier(mod_valuesToLowerCase);
        m_currentBlockHeader.header.performModifier(mod_removeLeadingZerosFromHexValuesEVEN);
        m_currentBlockHeader.isImportRawBlock = true;

        if (m_currentBlockHeader.header.atKey("extraData").asString().size() > 66)  // 0x + 32 bytes
            throw dev::RLPException("extraData is too long");

        // block transactions
        m_transactions.clear();
        if (!blockRLP[1].isList())
            throw dev::RLPException("Transactions RLP is expected to be list");

        for (auto const& tr : blockRLP[1])
        {
            if (!tr.isList())
                throw dev::RLPException("Transaction RLP is expected to be list");

            DataObject trInfo;
            // 0 - nonce        3 - to      6 - v
            // 1 - gasPrice     4 - value   7 - r
            // 2 - gasLimit     5 - data    8 - s
            std::ostringstream stream;
            stream << tr[5];  // !!! doesnt work for data > 32 bytes

            trInfo["data"] = stream.str() == "0x0" ? "" : rlpToString(tr[5], true);
            trInfo["gasLimit"] = rlpToString(tr[2], true);
            trInfo["gasPrice"] = rlpToString(tr[1], true);
            trInfo["nonce"] = rlpToString(tr[0], true);
            string const to = rlpToString(tr[3], true);
            trInfo["to"] = (to == "0x00") ? string() : to;
            trInfo["value"] = rlpToString(tr[4], true);
            trInfo["v"] = rlpToString(tr[6], true);
            trInfo["r"] = rlpToString(tr[7], true);
            trInfo["s"] = rlpToString(tr[8], true);
            if (trInfo["r"].asString().length() == 64)  // leading zeros removed. return it back
                trInfo["r"] = string(trInfo["r"].asString()).insert(2, "00");
            if (trInfo["s"].asString().length() == 64)  // leading zeros removed. return it back
                trInfo["s"] = string(trInfo["s"].asString()).insert(2, "00");
            eth_sendRawTransaction(scheme_transaction(trInfo));
        }

        // Caclulate uncle hash
        if (!blockRLP[2].isList())
            throw dev::RLPException("Uncles expected to be list");

        for (auto const& uncl : blockRLP[2])
        {
            // 0 - parentHash           // 8 - number
            // 1 - uncleHash            // 9 - gasLimit
            // 2 - coinbase             // 10 - gasUsed
            // 3 - stateRoot            // 11 - timestamp
            // 4 - transactionTrie      // 12 - extraData
            // 5 - receiptTrie          // 13 - mixHash
            // 6 - bloom                // 14 - nonce
            // 7 - difficulty

            DataObject uncleData;
            uncleData["extraData"] = rlpToString(uncl[12]);
            uncleData["gasLimit"] = rlpToString(uncl[9]);
            uncleData["gasUsed"] = rlpToString(uncl[10]);
            uncleData["logsBloom"] = rlpToString(uncl[6]);
            uncleData["miner"] = rlpToString(uncl[2]);
            uncleData["number"] = rlpToString(uncl[8]);
            uncleData["parentHash"] = rlpToString(uncl[0]);
            uncleData["receiptsRoot"] = rlpToString(uncl[5]);
            uncleData["sha3Uncles"] = rlpToString(uncl[1]);
            uncleData["stateRoot"] = rlpToString(uncl[3]);
            uncleData["timestamp"] = rlpToString(uncl[11]);
            uncleData["transactionsRoot"] = rlpToString(uncl[4]);
            uncleData["difficulty"] = rlpToString(uncl[7]);

            uncleData["hash"] = dev::toHexPrefixed(h256(0));
            uncleData["size"] = "0x00";
            uncleData["transactions"] = DataObject(DataType::Array);
            uncleData["uncles"] = DataObject(DataType::Array);
            scheme_RPCBlock uncleBlock(uncleData);

            // recalculate hash from fields
            uncleBlock.overwriteBlockHeader(uncleBlock.getBlockHeader());

            string const& unclePHash = uncleBlock.getBlockHeader().atKey("parentHash").asString();
            if (unclePHash == rawImportedBlockParentHash.asString())
                throw dev::RLPException("Uncle is brother (deried from the same block)");
            bool foundUncestor = false;
            for (auto const& bl : getCurrChain())
            {
                if (bl.getHash() == unclePHash)
                {
                    foundUncestor = true;
                    break;
                }
            }
            if (!foundUncestor)
                throw dev::RLPException("Uncle is derived from unknown block!");

            // check dublicated uncles
            for (auto const& bl : getCurrChain())
            {
                if (bl.getHash() == uncleBlock.getBlockHash())
                    throw dev::RLPException("Uncle is one of the blocks imported to chain!");

                for (auto const& un : bl.getRPCResponse().getUncles())
                {
                    if (un.asString() == uncleBlock.getBlockHash())
                        throw dev::RLPException("Dublicate uncles in chain");
                }
            }
            for (auto const& uncleRPC : m_currentBlockHeader.uncles)
            {
                if (uncleRPC.getBlockHash() == uncleBlock.getBlockHash())
                    throw dev::RLPException("Uncle already been attached to this block!");
            }
            m_currentBlockHeader.uncles.push_back(uncleBlock);
        }

        // Execute Raw Block
        mod_valuesToLowerCase(rawImportedBlockParentHash);
        // rawImportedBlockParentHash.performModifier(mod_removeLeadingZerosFromHexValuesEVEN);
        m_currentBlockHeader.currentBlockNumber = test::hexOrDecStringToInt(rlpToString(header[8]));
        m_currentBlockHeader.header["parentHash"] = rawImportedBlockParentHash.asString();
        m_currentBlockHeader.header.performVerifier(test::ver_checkHash32Fields);
    }
    catch (RLPException const& _ex)
    {
        // invalid rlp exception
        doChainReorg();
        m_lastInterfaceError["message"] = "Import raw block failed";
        m_lastInterfaceError["error"] = string("Error parsing block rlp: ") + _ex.what();
        ETH_TEST_MESSAGE(m_lastInterfaceError.asJson());
        ETH_TEST_MESSAGE("Response test_importRawBlock: " + rawBlockHash);
        return string();
    }

    test_mineBlocks(1);
    ToolBlock const& cbl = getLastBlock();
    if (cbl.wereInvalidTransactions())  // when import from raw block. no invalid transactions
                                        // (tool::rejected) allowed
    {
        m_blockchainMap.at(m_current_chain_ind).pop_back();
        doChainReorg();
        m_lastInterfaceError["message"] = "Import raw block failed";
        m_lastInterfaceError["error"] = "Raw block transaction execution failed!";
        ETH_TEST_MESSAGE(m_lastInterfaceError.asJson());
        ETH_TEST_MESSAGE("Response test_importRawBlock: " + rawBlockHash);
        return string();
    }

    ETH_ERROR_REQUIRE_MESSAGE(rawBlockHash == cbl.getHash(),
        string("RAW import hash != test_mineBlocks imitating rawblock. \n") +
            "RAW: " + rawBlockHash + " vs " + cbl.getHash());

    ETH_ERROR_REQUIRE_MESSAGE(
        rawImportedBlockNumber < 10000, "Sanitize blocknumber convertion error!");
    ETH_ERROR_REQUIRE_MESSAGE(getCurrChain().size() == rawImportedBlockNumber,
        string("If we import Rawblock with number >= 1, blockchain must have `number` blocks! \n") +
            "Blockchain size: " + to_string(getCurrChain().size()) + " vs " +
            to_string(rawImportedBlockNumber));

    // Check that parent hash of rawImportedBlock is equal to previous block known
    if (rawImportedBlockNumber == 1)
    {
        ETH_ERROR_REQUIRE_MESSAGE(getGenesis().getHash() == rawImportedBlockParentHash.asString(),
            "Unknown genesis: " + rawImportedBlockParentHash.asString());
    }
    else
    {
        ETH_ERROR_REQUIRE_MESSAGE(
            rawImportedBlockNumber > 1, "ImportRaw block require number >= 1");
        ToolBlock const& pbl = getLastBlock(2);  // previous block
        ETH_ERROR_REQUIRE_MESSAGE(
            pbl.getHash() == cbl.getRPCResponse().getBlockHeader().atKey("parentHash").asString(),
            "ImportRaw block require previous block hash == block.parentHash");
    }

    ETH_TEST_MESSAGE("Response test_importRawBlock: " + rawBlockHash);
    doChainReorg();
    return rawBlockHash;
}

std::string ToolImpl::test_getLogHash(std::string const& _txHash)
{
    rpcCall("", {});
    ETH_TEST_MESSAGE("Request: test_getLogHash " + _txHash);
    for (auto const& bl : getCurrChain())  // acutally need to look up all chains. but not needed.
                                           // cause only in state tests
    {
        for (auto const& receipt : bl.getRPCResponse().getTransactions())
        {
            if (receipt.atKey("hash").asString() == _txHash)
                return bl.getRPCResponse().getLogsHash();
        }
    }
    ETH_WARNING("test_getLogHash _txHash `" + _txHash + "' not found!");
    return "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347";
}


// Internal
DataObject ToolImpl::rpcCall(
    std::string const& _methodName, std::vector<std::string> const& _args, bool _canFail)
{
    m_lastInterfaceError.clear();
    if (Options::get().rpcLimit != 0 && Options::get().rpcLimit < m_totalCalls++)
        ETH_FAIL_MESSAGE("Request: rpcCall of test limit");
    (void)_methodName;
    (void)_args;
    (void)_canFail;
    return DataObject();
}

Socket::SocketType ToolImpl::getSocketType() const
{
    return m_sockType;
}

std::string const& ToolImpl::getSocketPath() const
{
    return m_toolPath;
}
