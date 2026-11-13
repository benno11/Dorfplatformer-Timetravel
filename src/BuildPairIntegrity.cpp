#include "BuildPairIntegrity.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace BuildPair {
namespace {
constexpr uint8_t magic[8] = {'D','F','P','A','I','R','0','1'};
constexpr size_t signatureOffset = 176;
uint16_t u16(const Bytes& b, size_t p) { return uint16_t(b[p]) | (uint16_t(b[p+1]) << 8); }
uint32_t u32(const Bytes& b, size_t p) {
    return uint32_t(b[p]) | (uint32_t(b[p+1]) << 8) | (uint32_t(b[p+2]) << 16) | (uint32_t(b[p+3]) << 24);
}
void put32(uint8_t* p, uint32_t value) { for (size_t i=0;i<4;++i) p[i] = uint8_t(value >> (8*i)); }
bool range(size_t start, size_t length, size_t total) { return start <= total && length <= total-start; }
bool overlaps(size_t a, size_t alen, size_t b, size_t blen) { return a < b+blen && b < a+alen; }
bool fail(std::string& error, const char* message) { error = message; return false; }
void checksum(Bytes& file, size_t offset) {
    put32(file.data()+offset, 0);
    uint64_t sum = 0;
    for (size_t p=0;p<file.size();p+=2) {
        const auto word = uint16_t(file[p]) | (p+1<file.size() ? uint16_t(file[p+1]) << 8 : 0);
        sum += word; sum = (sum & 0xffff) + (sum >> 16);
    }
    sum = (sum & 0xffff) + (sum >> 16);
    put32(file.data()+offset, uint32_t(sum + file.size()));
}
}
bool inspect(const Bytes& file, Image& image, std::string& error) {
    image = {};
    if (file.size() > 512ULL*1024*1024 || file.size()<64 || file[0]!='M' || file[1]!='Z')
        return fail(error,"Invalid Windows executable header.");
    const size_t pe = u32(file,0x3c);
    if (pe<64 || !range(pe,24,file.size()) || std::memcmp(file.data()+pe,"PE\0\0",4))
        return fail(error,"Invalid PE header.");
    const size_t count = u16(file,pe+6), optionalSize=u16(file,pe+20), optional=pe+24;
    if (!count || count>96 || optionalSize<96 || !range(optional,optionalSize,file.size()))
        return fail(error,"Invalid PE optional header.");
    const auto format = u16(file,optional);
    if ((format!=0x10b && format!=0x20b) || (format==0x20b && optionalSize<112))
        return fail(error,"Unsupported PE image format.");
    image.machine=u16(file,pe+4);
    if ((image.machine==0x14c && format!=0x10b) ||
        ((image.machine==0x8664 || image.machine==0xaa64) && format!=0x20b) ||
        (image.machine!=0x14c && image.machine!=0x8664 && image.machine!=0xaa64))
        return fail(error,"Unsupported PE architecture.");
    image.dll = (u16(file,pe+22)&0x2000)!=0;
    image.checksumOffset=optional+64;
    const size_t table=optional+optionalSize;
    const size_t headers=u32(file,optional+60);
    if (!range(table,count*40,file.size()) || headers<table+count*40 || headers>file.size())
        return fail(error,"Invalid PE section table.");
    struct Span { size_t start,size; };
    std::vector<Span> spans;
    bool found=false;
    for (size_t n=0;n<count;++n) {
        const size_t p=table+n*40;
        const size_t size=u32(file,p+16), raw=u32(file,p+20);
        if (size && (raw<headers || !range(raw,size,file.size())))
            return fail(error,"PE section is outside the file.");
        for (const auto& span:spans)
            if (size && overlaps(raw,size,span.start,span.size))
                return fail(error,"Overlapping PE sections.");
        if (size) spans.push_back({raw,size});
        constexpr uint8_t name[8]={'.','d','f','p','a','i','r',0};
        if (std::memcmp(file.data()+p,name,8)) continue;
        const uint32_t flags=u32(file,p+36);
        if (found || u32(file,p+8)!=sealSize || size<sealSize || (flags&0xa0000000U) || !(flags&0x40000000U))
            return fail(error,"Invalid or duplicate .dfpair signature section.");
        found=true; image.sealOffset=raw;
    }
    if (!found) return fail(error,"The .dfpair signature section is missing.");
    return true;
}
std::array<uint8_t,digestSize> digest(const Bytes& file,const Image& image) {
    std::array<uint8_t,digestSize> result{};
    std::array<uint8_t,sealSize> zero{};
    crypto_blake2b_ctx hash;
    crypto_blake2b_init(&hash,result.size());
    // PE checksum is in the headers, before all raw section data.
    crypto_blake2b_update(&hash,file.data(),image.checksumOffset);
    crypto_blake2b_update(&hash,zero.data(),4);
    const size_t afterChecksum=image.checksumOffset+4;
    crypto_blake2b_update(&hash,file.data()+afterChecksum,image.sealOffset-afterChecksum);
    crypto_blake2b_update(&hash,zero.data(),sealSize);
    const size_t afterSeal=image.sealOffset+sealSize;
    crypto_blake2b_update(&hash,file.data()+afterSeal,file.size()-afterSeal);
    crypto_blake2b_final(&hash,result.data());
    return result;
}
Seal getSeal(const Bytes& file,const Image& image) {
    Seal result{};
    std::copy_n(file.data()+image.sealOffset,sealSize,result.begin());
    return result;
}
bool verify(const Bytes& exe,const Bytes& dll,const PublicKey& key,uint32_t edition,std::string& error,const Seal* loadedExeSeal) {
    Image launcher,game;
    if (!inspect(exe,launcher,error) || !inspect(dll,game,error)) return false;
    if (launcher.dll || !game.dll || launcher.machine!=game.machine)
        return fail(error,"The launcher and game architectures do not match.");
    const auto proof=getSeal(exe,launcher), gameProof=getSeal(dll,game);
    if (proof!=gameProof || (loadedExeSeal && proof!=*loadedExeSeal))
        return fail(error,"The launcher and game are from different builds.");
    if (std::memcmp(proof.data(),magic,8) || proof[8]!=1 || proof[9] || proof[10] || proof[11] ||
        proof[12]!=edition || proof[13] || proof[14] || proof[15] || edition>1 ||
        std::any_of(proof.begin()+240,proof.end(),[](uint8_t b){return b!=0;}) ||
        std::all_of(proof.begin()+16,proof.begin()+48,[](uint8_t b){return b==0;}))
        return fail(error,"The build signature is missing or unsupported.");
    if (crypto_ed25519_check(proof.data()+signatureOffset,key.data(),proof.data(),signatureOffset))
        return fail(error,"The build signature is invalid.");
    const auto exeHash=digest(exe,launcher), dllHash=digest(dll,game);
    if (crypto_verify64(exeHash.data(),proof.data()+48) || crypto_verify64(dllHash.data(),proof.data()+112))
        return fail(error,"The launcher or game code has been modified.");
    error.clear(); return true;
}
bool seal(Bytes& exe,Bytes& dll,const std::array<uint8_t,32>& seed,const std::array<uint8_t,32>& buildId,uint32_t edition,std::string& error) {
    Image launcher,game;
    if (!inspect(exe,launcher,error) || !inspect(dll,game,error)) return false;
    if (launcher.dll || !game.dll || launcher.machine!=game.machine || edition>1 ||
        std::all_of(buildId.begin(),buildId.end(),[](uint8_t b){return b==0;}))
        return fail(error,"Cannot sign an incompatible build pair.");
    Seal proof{};
    std::copy_n(magic,8,proof.begin()); put32(proof.data()+8,1);put32(proof.data()+12,edition);
    std::copy(buildId.begin(),buildId.end(),proof.begin()+16);
    const auto exeHash=digest(exe,launcher), dllHash=digest(dll,game);
    std::copy(exeHash.begin(),exeHash.end(),proof.begin()+48);
    std::copy(dllHash.begin(),dllHash.end(),proof.begin()+112);
    std::array<uint8_t,32> seedCopy=seed, pub{};std::array<uint8_t,64> secret{};
    crypto_ed25519_key_pair(secret.data(),pub.data(),seedCopy.data());
    crypto_ed25519_sign(proof.data()+signatureOffset,secret.data(),proof.data(),signatureOffset);
    crypto_wipe(secret.data(),secret.size());crypto_wipe(seedCopy.data(),seedCopy.size());
    std::copy(proof.begin(),proof.end(),exe.begin()+launcher.sealOffset);
    std::copy(proof.begin(),proof.end(),dll.begin()+game.sealOffset);
    checksum(exe,launcher.checksumOffset);checksum(dll,game.checksumOffset);
    return verify(exe,dll,pub,edition,error);
}
}
