#include "BuildPairIntegrity.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#endif
namespace fs = std::filesystem;
using Key = std::array<uint8_t,32>;
namespace {
struct PrivateKey {
    Key bytes{};
    ~PrivateKey(){crypto_wipe(bytes.data(),bytes.size());}
};
void randomBytes(uint8_t* bytes,size_t count) {
#ifdef _WIN32
    if(BCryptGenRandom(nullptr,bytes,static_cast<ULONG>(count),BCRYPT_USE_SYSTEM_PREFERRED_RNG))
        throw std::runtime_error("Secure randomness unavailable");
#else
    int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0)throw std::runtime_error("Secure randomness unavailable");
    size_t pos=0;
    while(pos<count){auto n=read(fd,bytes+pos,count-pos);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);throw std::runtime_error("Secure randomness unavailable");}pos+=n;}
    close(fd);
#endif
}
BuildPair::Bytes readFile(const fs::path& path,size_t limit=512ULL*1024*1024) {
    const auto size=fs::file_size(path);if(size>limit)throw std::runtime_error("Input file exceeds size limit");
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot open input file");
    BuildPair::Bytes bytes(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size));
    if(!in||in.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Cannot read a stable input file");
    return bytes;
}
std::string hex(const uint8_t* data,size_t size) {
    constexpr char digits[]="0123456789abcdef";std::string result;
    for(size_t i=0;i<size;++i){result+=digits[data[i]>>4];result+=digits[data[i]&15];}return result;
}
void replaceFile(const fs::path& path,const BuildPair::Bytes& bytes) {
    Key nonce{};randomBytes(nonce.data(),nonce.size());auto temp=path;temp+=".seal-"+hex(nonce.data(),8);
#ifdef _WIN32
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create sealed output");
    DWORD count=0;bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size();
    if(ok)ok=FlushFileBuffers(file)!=FALSE;
    if(!CloseHandle(file))ok=false;
    if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("Cannot replace sealed output (is the game running?)");}
#else
    struct stat st{};const bool exists=stat(path.c_str(),&st)==0;
    int fd=open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,exists?(st.st_mode&0777):0600);
    if(fd<0)throw std::runtime_error("Cannot create sealed output");
    bool ok=true;size_t pos=0;
    while(pos<bytes.size()){auto n=write(fd,bytes.data()+pos,bytes.size()-pos);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}pos+=n;}
    if(ok)ok=fsync(fd)==0;if(close(fd))ok=false;
    if(ok)ok=rename(temp.c_str(),path.c_str())==0;
    if(!ok){unlink(temp.c_str());throw std::runtime_error("Cannot replace sealed output");}
#endif
}
void keygen(const fs::path& privatePath,const fs::path& headerPath) {
    fs::create_directories(privatePath.parent_path());
    PrivateKey seed;
    if(fs::exists(privatePath)) {
        auto bytes=readFile(privatePath,32);
        if(bytes.size()!=32)throw std::runtime_error("Invalid build signing key");
        std::copy(bytes.begin(),bytes.end(),seed.bytes.begin());crypto_wipe(bytes.data(),bytes.size());
    } else {
        randomBytes(seed.bytes.data(),seed.bytes.size());
#ifdef _WIN32
        HANDLE file=CreateFileW(privatePath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create private build signing key");
        DWORD written=0;bool ok=WriteFile(file,seed.bytes.data(),32,&written,nullptr)&&written==32;
        if(ok)ok=FlushFileBuffers(file)!=FALSE;if(!CloseHandle(file))ok=false;
        if(!ok)throw std::runtime_error("Cannot persist private build signing key");
#else
        fs::permissions(privatePath.parent_path(),fs::perms::owner_all,fs::perm_options::replace);
        int fd=open(privatePath.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd<0)throw std::runtime_error("Cannot create private build signing key");
        size_t pos=0;bool ok=true;
        while(pos<32){auto n=write(fd,seed.bytes.data()+pos,32-pos);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}pos+=n;}
        if(ok)ok=fsync(fd)==0;if(close(fd))ok=false;
        if(!ok)throw std::runtime_error("Cannot persist private build signing key");
#endif
    }
    Key publicKey{},seedCopy=seed.bytes;std::array<uint8_t,64> secret{};
    crypto_ed25519_key_pair(secret.data(),publicKey.data(),seedCopy.data());crypto_wipe(secret.data(),secret.size());crypto_wipe(seedCopy.data(),seedCopy.size());
    std::ostringstream text;
    text<<"// Generated public verification key only. Never distribute the private key.\n#pragma once\n#include \"BuildPairIntegrity.h\"\nnamespace BuildPair {\ninline constexpr PublicKey trustedPublicKey = {";
    for(size_t i=0;i<publicKey.size();++i){if(i)text<<',';text<<unsigned(publicKey[i]);}
    text<<"};\n}\n";
    const auto value=text.str();
    if(fs::exists(headerPath)){auto old=readFile(headerPath,8192);if(std::string(old.begin(),old.end())==value)return;}
    replaceFile(headerPath,BuildPair::Bytes(value.begin(),value.end()));
}
}
int main(int argc,char** argv) {
    try {
        if(argc==4 && std::string(argv[1])=="keygen") {
            keygen(fs::u8path(argv[2]),fs::u8path(argv[3]));return 0;
        }
        if(argc!=6 || (std::string(argv[1])!="seal" && std::string(argv[1])!="verify")) {
            std::cerr<<"Usage: build_pair_signer keygen PRIVATE_KEY PUBLIC_HEADER\n"
                <<"       build_pair_signer seal|verify PRIVATE_KEY EXE DLL full|lite\n";return 2;
        }
        const std::string editionName=argv[5];
        if(editionName!="full"&&editionName!="lite")throw std::runtime_error("Invalid edition");
        const uint32_t edition=editionName=="lite"?1:0;
        PrivateKey seed;auto bytes=readFile(fs::u8path(argv[2]),32);
        if(bytes.size()!=32)throw std::runtime_error("Invalid build signing key");
        std::copy(bytes.begin(),bytes.end(),seed.bytes.begin());crypto_wipe(bytes.data(),bytes.size());
        auto exe=readFile(fs::u8path(argv[3])),dll=readFile(fs::u8path(argv[4]));
        Key publicKey{},seedCopy=seed.bytes;std::array<uint8_t,64> secret{};
        crypto_ed25519_key_pair(secret.data(),publicKey.data(),seedCopy.data());crypto_wipe(secret.data(),secret.size());crypto_wipe(seedCopy.data(),seedCopy.size());
        std::string error;
        if(BuildPair::verify(exe,dll,publicKey,edition,error)) {std::cout<<"Build pair signature verified\n";return 0;}
        if(std::string(argv[1])=="verify")throw std::runtime_error(error);
        Key buildId{};randomBytes(buildId.data(),buildId.size());
        if(!BuildPair::seal(exe,dll,seed.bytes,buildId,edition,error))throw std::runtime_error(error);
        // Write the DLL last, so it remains newer than its EXE link dependency.
        replaceFile(fs::u8path(argv[3]),exe);replaceFile(fs::u8path(argv[4]),dll);
        std::cout<<"Signed matching launcher and game .dfpair sections\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"Build pair signing failed: "<<error.what()<<"\n";return 1;}
}
