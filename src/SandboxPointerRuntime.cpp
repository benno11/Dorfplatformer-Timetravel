#include "SandboxPointers.h"
#include "SandboxUpdates.h"
#include "monocypher.h"
#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
namespace SandboxPointers {
namespace {
[[noreturn]] void violation() {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(),0xC0000409UL);
#endif
    std::abort();
}
void randomKey(uint8_t* out) {
#ifdef _WIN32
    if(BCryptGenRandom(nullptr,out,32,BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)violation();
#elif defined(__APPLE__)
    if(SecRandomCopyBytes(kSecRandomDefault,32,out)!=errSecSuccess)violation();
#else
    int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0)violation();
    size_t pos=0;while(pos<32){auto n=::read(fd,out+pos,32-pos);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);violation();}pos+=n;}close(fd);
#endif
}
enum Kind : uint32_t { Buffer=1, Immutable, Game, ModLoad, ModCallbacks };
struct Entry {
    uint64_t id=0;
    Kind kind=Buffer;
    size_t size=0;
    std::vector<uint8_t> data;
    DFSandboxGameEntry game=nullptr;
    DFModLoad load=nullptr;
    DFModCallbacks callbacks{};
    std::array<uint8_t,32> metadataSeal{};
};
class Registry {
    std::array<uint8_t,32> key_{};
    std::mutex mutex_;
    std::map<uint64_t,std::shared_ptr<Entry>> entries_;
    uint64_t next_=1;
    size_t allocated_=0;
    void tag(const Entry& e,uint8_t* result,bool content=true) {
        crypto_blake2b_ctx hash;
        crypto_blake2b_keyed_init(&hash,32,key_.data(),key_.size());
        auto add=[&](const auto& value){crypto_blake2b_update(&hash,reinterpret_cast<const uint8_t*>(&value),sizeof(value));};
        // Authenticate identity, type, extent, backing address and jump target.
        // Immutable content/callback pointers are authenticated as well.
        add(e.id);add(e.kind);add(e.size);
        auto address=e.data.data();add(address);add(e.game);add(e.load);
        auto backingSize=e.data.size();add(backingSize);
        if(content&&e.kind==Immutable)crypto_blake2b_update(&hash,e.data.data(),e.data.size());
        if(content&&e.kind==ModCallbacks) add(e.callbacks);
        crypto_blake2b_final(&hash,result);
    }
    std::shared_ptr<Entry> checked(DFSandboxHandle handle) {
        auto found=entries_.find(handle.id);if(found==entries_.end())violation();
        auto e=found->second;uint8_t expected[32];
        tag(*e,expected,false);
        if(crypto_verify32(e->metadataSeal.data(),expected))violation();
        tag(*e,expected);
        if(crypto_verify32(handle.authentication,expected))violation();
        return e;
    }
public:
    Registry(){randomKey(key_.data());}
    ~Registry(){crypto_wipe(key_.data(),key_.size());}
    DFSandboxHandle add(std::shared_ptr<Entry> e) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(next_==0||entries_.size()>=65536||e->size>64*1024*1024||allocated_>256*1024*1024-e->size)violation();
        e->id=next_++;allocated_+=e->size;tag(*e,e->metadataSeal.data(),false);entries_.emplace(e->id,e);
        DFSandboxHandle result{};result.id=e->id;tag(*e,result.authentication);return result;
    }
    void access(DFSandboxHandle handle,size_t offset,void* bytes,size_t count,bool write) {
        std::lock_guard<std::mutex> lock(mutex_);auto e=checked(handle);
        if((e->kind!=Buffer&&e->kind!=Immutable)||(write&&e->kind!=Buffer)||offset>e->size||count>e->size-offset||(!bytes&&count))violation();
        if(count) {if(write)std::memcpy(e->data.data()+offset,bytes,count);else std::memcpy(bytes,e->data.data()+offset,count);}
    }
    void release(DFSandboxHandle handle) {
        std::lock_guard<std::mutex> lock(mutex_);auto e=checked(handle);allocated_-=e->size;entries_.erase(handle.id);
        if(!e->data.empty())crypto_wipe(e->data.data(),e->data.size());
    }
    std::shared_ptr<const Entry> function(DFSandboxHandle handle,Kind kind) {
        std::lock_guard<std::mutex> lock(mutex_);auto e=checked(handle);if(e->kind!=kind)violation();return e;
    }
};
Registry& registry(){static Registry value;return value;}
DFSandboxHandle buffer(const void* data,size_t size,bool immutable) {
    if(size>64*1024*1024||(!data&&size&&immutable))violation();
    auto e=std::make_shared<Entry>();e->kind=immutable?Immutable:Buffer;e->size=size;e->data.resize(size);
    if(immutable&&size)std::memcpy(e->data.data(),data,size);
    return registry().add(e);
}
const DFSandboxAPI apiValue={
    sizeof(DFSandboxAPI),DF_SANDBOX_API_VERSION,
    [](size_t size){return buffer(nullptr,size,false);},
    [](const void* data,size_t size){return buffer(data,size,true);},
    [](DFSandboxHandle h,size_t offset,void* bytes,size_t count){registry().access(h,offset,bytes,count,false);},
    [](DFSandboxHandle h,size_t offset,const void* bytes,size_t count){registry().access(h,offset,const_cast<void*>(bytes),count,true);},
    [](DFSandboxHandle h){registry().release(h);},
    [](DFSandboxGameEntry target){if(!target)violation();auto e=std::make_shared<Entry>();e->kind=Game;e->game=target;return registry().add(e);},
    [](DFSandboxHandle h,int argc,char** argv){auto e=registry().function(h,Game);return e->game(argc,argv,runtimeAPI());},
    [](DFModLoad target){if(!target)violation();auto e=std::make_shared<Entry>();e->kind=ModLoad;e->load=target;return registry().add(e);},
    [](DFSandboxHandle h,const DFModContext* context,DFModCallbacks* callbacks){auto e=registry().function(h,ModLoad);return e->load(context,callbacks);},
    [](const DFModCallbacks* callbacks){if(!callbacks)violation();auto e=std::make_shared<Entry>();e->kind=ModCallbacks;e->callbacks=*callbacks;return registry().add(e);},
    [](DFSandboxHandle h,const DFLevelState* level){auto e=registry().function(h,ModCallbacks);if(e->callbacks.on_level_start)e->callbacks.on_level_start(e->callbacks.userdata,level);},
    [](DFSandboxHandle h,double seconds){auto e=registry().function(h,ModCallbacks);if(e->callbacks.on_update)e->callbacks.on_update(e->callbacks.userdata,seconds);},
    [](DFSandboxHandle h){auto e=registry().function(h,ModCallbacks);if(e->callbacks.on_unload)e->callbacks.on_unload(e->callbacks.userdata);},
    [](DFSandboxHandle h,const DFMenuAPI* api){auto e=registry().function(h,ModCallbacks);if(e->callbacks.on_menu_load)e->callbacks.on_menu_load(e->callbacks.userdata,api);},
    [](DFSandboxHandle h,const char* menu,const char* action){auto e=registry().function(h,ModCallbacks);if(e->callbacks.on_menu_action)e->callbacks.on_menu_action(e->callbacks.userdata,menu,action);},
    [](uint32_t action)->int32_t{return SandboxUpdates::request(action)?1:0;},
    [](DFSandboxUpdateStatus* status)->int32_t{
        if(!status||status->struct_size<sizeof(DFSandboxUpdateStatus))return 0;
        *status=SandboxUpdates::snapshot();return 1;
    }
};
}
const DFSandboxAPI* runtimeAPI(){return &apiValue;}
}
