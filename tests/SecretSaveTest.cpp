#include "SecretSave.h"
#include "monocypher-ed25519.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#endif
using namespace SecretData;
std::string bytes(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void put(const std::filesystem::path& p,const std::string& s){std::ofstream out(p,std::ios::binary);out.write(s.data(),s.size());}
int main(int argc,char** argv){
    auto root=std::filesystem::temp_directory_path()/"df-secret-test";
    std::filesystem::remove_all(root);std::filesystem::create_directories(root);
    Key key{};for(size_t i=0;i<32;++i)key[i]=i;
    KeyProvider provider=[&](bool,Key& out){out=key;return true;};
    nlohmann::json data={{"version",1},{"achievements",{{"unlocked",{"complete_level"}}}},
        {"account",{{"server_url","https://test.invalid"},{"username","private-user"},{"auth_token","private-token-unique"}}}};
    Store store(root,"test",provider);nlohmann::json loaded;
    assert(store.load(loaded)==Store::Result::Missing);
    assert(store.save(data));auto path=root/"savedata_secret";auto first=bytes(path);
    assert(first.find("private-user")==std::string::npos&&first.find("private-token-unique")==std::string::npos);
    assert(first.find("complete_level")==std::string::npos);
    assert(store.load(loaded)==Store::Result::Loaded&&loaded==data);
    assert(store.save(data));auto second=bytes(path);assert(second!=first);
    Store fresh(root,"test",provider);assert(fresh.load(loaded)==Store::Result::Loaded&&loaded==data);
    Store wrongContext(root,"other",provider);assert(wrongContext.load(loaded)==Store::Result::Error);
    KeyProvider wrong=[](bool,Key& out){out.fill(99);return true;};
    Store wrongKey(root,"test",wrong);assert(wrongKey.load(loaded)==Store::Result::Error);
    for(auto position:{size_t(0),size_t(8),size_t(32),size_t(48),second.size()-1}) {
        auto changed=second;changed[position]^=1;put(path,changed);
        Store tampered(root,"test",provider);assert(tampered.load(loaded)==Store::Result::Error);
        assert(!tampered.save(data)&&bytes(path)==changed);
        // A store already open must also refuse overwriting a subsequently damaged file.
        assert(!fresh.save(data)&&bytes(path)==changed);
    }
    put(path,second);
    bool requestedCreate=false;
    Store missingKey(root,"test",[&](bool create,Key&){requestedCreate=create;return false;});
    assert(missingKey.load(loaded)==Store::Result::Error&&!requestedCreate);
    assert(!missingKey.save(data)&&bytes(path)==second);
    auto publicData=data;publicData["level_api_auth_token"]="token";
    publicData["settings"]["network"]={{"level_server_auth_token","token"},{"level_api_account_username","user"},{"level_server_url","safe"}};
    scrubLegacy(publicData);assert(!publicData.contains("achievements")&&!publicData.contains("level_api_auth_token"));
    assert(publicData["settings"]["network"].size()==1);
    // RFC 8032, test 1: standard Ed25519 empty-message signature.
    auto unhex=[](const char* hex){std::vector<uint8_t> out;for(size_t i=0;hex[i];i+=2){unsigned b=0;std::sscanf(hex+i,"%2x",&b);out.push_back(b);}return out;};
    auto seed=unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    auto expected=unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    uint8_t secret[64],pub[32],signature[64];crypto_ed25519_key_pair(secret,pub,seed.data());crypto_ed25519_sign(signature,secret,nullptr,0);
    assert(std::equal(expected.begin(),expected.end(),signature));
#if defined(__linux__) && !defined(__ANDROID__)
    auto local=root/"local";Store localStore(local,"test");assert(localStore.save(data));
    Store localReload(local,"test");assert(localReload.load(loaded)==Store::Result::Loaded&&loaded==data);
    struct stat st{};assert(!stat((local/".keys/savedata_secret.key").c_str(),&st)&&(st.st_mode&077)==0);
    assert(!stat((local/"savedata_secret").c_str(),&st)&&(st.st_mode&077)==0);
    std::filesystem::remove(local/".keys/savedata_secret.key");Store lost(local,"test");
    assert(lost.load(loaded)==Store::Result::Error&&!lost.save(data));
    assert(!std::filesystem::exists(local/".keys/savedata_secret.key"));
#endif
    // Optional fixed-key fixture for independent libsodium verification.
    if(argc>1){Store reference(std::filesystem::u8path(argv[1]),"test",provider);assert(reference.save(data));}
    std::filesystem::remove_all(root);
    std::cout<<"Secret-save encryption, signature, tamper and key-loss tests passed\n";
}
