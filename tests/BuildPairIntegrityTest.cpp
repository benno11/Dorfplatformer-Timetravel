#include "BuildPairIntegrity.h"
#include "monocypher-ed25519.h"
#include <cassert>
#include <algorithm>
#include <iostream>
using namespace BuildPair;
static void put16(Bytes& b,size_t p,uint16_t value){b[p]=uint8_t(value);b[p+1]=uint8_t(value>>8);}
static void put32(Bytes& b,size_t p,uint32_t value){for(size_t i=0;i<4;++i)b[p+i]=uint8_t(value>>(i*8));}
static Bytes image(bool dll) {
    Bytes b(1536);
    b[0]='M';b[1]='Z';put32(b,0x3c,128);b[128]='P';b[129]='E';
    put16(b,132,0x8664);put16(b,134,2);put16(b,148,240);put16(b,150,dll?0x2022:0x22);
    put16(b,152,0x20b);put32(b,152+60,512);
    size_t section=152+240;
    const char text[]=".text";std::copy_n(text,5,b.begin()+section);
    put32(b,section+8,256);put32(b,section+12,4096);put32(b,section+16,512);put32(b,section+20,512);put32(b,section+36,0x60000020);
    section+=40;const char pair[]=".dfpair";std::copy_n(pair,7,b.begin()+section);
    put32(b,section+8,256);put32(b,section+12,8192);put32(b,section+16,512);put32(b,section+20,1024);put32(b,section+36,0x40000040);
    b[512]=dll?0xc3:0x90;
    return b;
}
int main(){
    auto exe=image(false),dll=image(true);std::string error;Image parsed;
    assert(inspect(exe,parsed,error)&&parsed.sealOffset==1024);
    std::array<uint8_t,32> seed{},id{},publicKey{};std::array<uint8_t,64> secret{};
    for(size_t i=0;i<32;++i){seed[i]=uint8_t(i);id[i]=uint8_t(i+1);}
    auto copy=seed;crypto_ed25519_key_pair(secret.data(),publicKey.data(),copy.data());
    assert(!verify(exe,dll,publicKey,0,error));
    assert(seal(exe,dll,seed,id,0,error));
    assert(verify(exe,dll,publicKey,0,error));
    auto loaded=getSeal(exe,parsed);
    assert(verify(exe,dll,publicKey,0,error,&loaded));
    auto wrongLoaded=loaded;wrongLoaded[16]^=1;assert(!verify(exe,dll,publicKey,0,error,&wrongLoaded));
    assert(!verify(exe,dll,publicKey,1,error));
    auto wrongKey=publicKey;wrongKey[0]^=1;assert(!verify(exe,dll,wrongKey,0,error));
    for(size_t position:{size_t(2),size_t(512),size_t(1024+8),size_t(1024+16),size_t(1024+48),size_t(1024+112),size_t(1024+176),size_t(1024+240),size_t(1400)}) {
        auto changed=dll;changed[position]^=1;assert(!verify(exe,changed,publicKey,0,error));
    }
    auto changedExe=exe;changedExe[512]^=1;assert(!verify(changedExe,dll,publicKey,0,error));
    auto overlay=dll;overlay.push_back(1);assert(!verify(exe,overlay,publicKey,0,error));
    // Even editing both matching proof copies cannot forge the Ed25519 signature.
    auto forgedExe=exe,forgedDll=dll;forgedExe[1024+112]^=1;forgedDll[1024+112]^=1;
    assert(!verify(forgedExe,forgedDll,publicKey,0,error));
    auto nextExe=exe,nextDll=dll;nextDll[513]^=1;id[0]^=1;
    assert(seal(nextExe,nextDll,seed,id,0,error));assert(verify(nextExe,nextDll,publicKey,0,error));
    assert(!verify(exe,nextDll,publicKey,0,error));assert(!verify(nextExe,dll,publicKey,0,error));
    auto exe32=image(false),dll32=image(true);
    put16(exe32,132,0x14c);put16(dll32,132,0x14c);put16(exe32,152,0x10b);put16(dll32,152,0x10b);
    assert(seal(exe32,dll32,seed,id,1,error));assert(verify(exe32,dll32,publicKey,1,error));
    assert(!verify(exe32,dll,publicKey,0,error));
    // Checksum bytes are non-executable bookkeeping and intentionally normalized.
    auto checksumOnly=dll;checksumOnly[parsed.checksumOffset]^=1;assert(verify(exe,checksumOnly,publicKey,0,error));
    for(size_t size:{size_t(0),size_t(63),size_t(150),size_t(512),size_t(1025)}) {
        auto truncated=dll;truncated.resize(size);assert(!inspect(truncated,parsed,error));
    }
    auto overlap=dll;put32(overlap,432+20,512);assert(!inspect(overlap,parsed,error));
    auto writable=dll;put32(writable,432+36,0xc0000040);assert(!inspect(writable,parsed,error));
    auto executable=dll;put32(executable,432+36,0x60000040);assert(!inspect(executable,parsed,error));
    auto oversized=dll;put32(oversized,432+16,0xffffffff);assert(!inspect(oversized,parsed,error));
    auto duplicate=dll;std::copy_n(".dfpair",7,duplicate.begin()+392);assert(!inspect(duplicate,parsed,error));
    std::cout<<"Signed PE pairs reject code/header/signature tampering, mismatched builds, editions, keys and malformed sections\n";
}
