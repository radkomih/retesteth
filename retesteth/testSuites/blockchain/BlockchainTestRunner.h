#pragma once
#include <retesteth/session/SessionInterface.h>
#include <retesteth/testStructures/types/BlockchainTests/BlockchainTest.h>
#include <retesteth/testSuiteRunner/TestSuite.h>
#include <string>

namespace test
{
class BlockchainTestRunner
{
public:
    BlockchainTestRunner(teststruct::BlockchainTestInFilled const&, TestSuite::TestSuiteOptions const&);
    static bool validateFork(std::string const& _testname, teststruct::FORK const& _net);
    bool abortBlock() const;
    void incrementBlockAndSetTestInfo();
    void setChainParams();
    void validateTransactionSequence(teststruct::BlockchainTestBlock const&);
    teststruct::FH32 mineBlock(teststruct::BYTES const&);
    teststruct::EthGetBlockBy requestBlock(teststruct::FH32 const&);
    teststruct::EthGetBlockBy requestBlock(teststruct::VALUE const&);
    bool checkLastRPCBlockException(teststruct::BlockchainTestBlock const&);
    void performOptionCommands(teststruct::BlockchainTestBlock const&, teststruct::EthGetBlockBy const&);
    void validateBlockHeader(teststruct::BlockchainTestBlock const&, teststruct::EthGetBlockBy const&);
    void validateUncles(teststruct::BlockchainTestBlock const&, teststruct::EthGetBlockBy const&);
    void validateTransactions(teststruct::BlockchainTestBlock const&, teststruct::EthGetBlockBy const&);
    test::session::SessionInterface& session() { return m_session; }
    void checkPostState(teststruct::EthGetBlockBy const&);
    void checkGenesis();

private:
    teststruct::BlockchainTestInFilled const& m_test;
    TestSuite::TestSuiteOptions const& m_opt;
    test::session::SessionInterface& m_session;
    size_t m_blockNumber = 0;
};

}  // namespace test