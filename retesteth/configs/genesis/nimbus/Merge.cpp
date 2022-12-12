#include <retesteth/configs/Options.h>
using namespace std;
using namespace dataobject;

namespace retesteth::options
{

const string nimbus_Merge_config = R"({
    "params" : {
        "fork" : "Merge",
        "terminalTotalDifficulty" : "0x00",
        "constantinopleForkBlock" : "0x00",
        "byzantiumForkBlock" : "0x00",
        "homesteadForkBlock" : "0x00",
        "chainID" : "0x01"
    },
    "accounts" : {
    }
})";

genMergeCfgNIMBUS::genMergeCfgNIMBUS()
{
    {
        spDataObject obj;
        (*obj)["path"] = "nimbus/genesis/Merge.json";
        (*obj)["content"] = nimbus_Merge_config;
        map_configs.addArrayObject(obj);
    }
}
}  // namespace retesteth::options
