#include "SecretSave.h"
#include "Platform.h"
#if PLATFORMER_IOS
#include "IosSecretKey.h"
#include "EditionPolicy.h"
#endif
#include "monocypher.h"
#include <fstream>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#elif defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <jni.h>
#endif
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
namespace SecretData {
bool randomBytes(unsigned char* out, size_t size) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr,out,static_cast<ULONG>(size),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0;
#elif defined(__APPLE__)
    return SecRandomCopyBytes(kSecRandomDefault,size,out)==errSecSuccess;
#else
    int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if(fd<0) return false;
    size_t pos=0;
    while(pos<size) { auto n=::read(fd,out+pos,size-pos); if(n<0&&errno==EINTR)continue; if(n<=0)break; pos+=n; }
    close(fd); return pos==size;
#endif
}
#ifdef __ANDROID__
static bool wrap(bool encrypt, const std::vector<unsigned char>& input, std::vector<unsigned char>& output) {
    auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
    if(!env||!activity)return false;
    auto cls=env->GetObjectClass(activity);
    auto method=env->GetMethodID(cls,encrypt?"protectSavedataKey":"unprotectSavedataKey","([B)[B");
    if(!method||env->ExceptionCheck()) {
        if(env->ExceptionCheck())env->ExceptionClear();
        env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);return false;
    }
    auto in=env->NewByteArray(static_cast<jsize>(input.size()));
    if(!in){if(env->ExceptionCheck())env->ExceptionClear();env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);return false;}
    env->SetByteArrayRegion(in,0,input.size(),reinterpret_cast<const jbyte*>(input.data()));
    auto out=method?static_cast<jbyteArray>(env->CallObjectMethod(activity,method,in)):nullptr;
    bool ok=!env->ExceptionCheck()&&out;
    if(env->ExceptionCheck())env->ExceptionClear();
    if(ok) { output.resize(env->GetArrayLength(out)); env->GetByteArrayRegion(out,0,output.size(),reinterpret_cast<jbyte*>(output.data())); }
    if(out) {
        if(!encrypt&&ok){std::vector<jbyte> zeros(output.size(),0);env->SetByteArrayRegion(out,0,zeros.size(),zeros.data());}
        env->DeleteLocalRef(out);
    }
    env->DeleteLocalRef(in); env->DeleteLocalRef(cls); env->DeleteLocalRef(activity);
    return ok;
}
#endif
#if PLATFORMER_IOS
namespace {
struct IosKeychain {
    KeychainRead read(const std::string& identity, Key& key) {
        auto service = CFStringCreateWithCString(nullptr, identity.c_str(), kCFStringEncodingUTF8);
        const void* names[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit};
        const void* values[] = {kSecClassGenericPassword, service, CFSTR("savedata_secret"), kCFBooleanTrue, kSecMatchLimitOne};
        auto query = CFDictionaryCreate(nullptr, names, values, 5, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFTypeRef result = nullptr;
        auto status = SecItemCopyMatching(query, &result);
        bool valid = status == errSecSuccess && result && CFGetTypeID(result) == CFDataGetTypeID() && CFDataGetLength((CFDataRef)result) == 32;
        if (valid) CFDataGetBytes((CFDataRef)result, CFRangeMake(0, 32), key.data());
        if (result) CFRelease(result);
        CFRelease(query); CFRelease(service);
        return valid ? KeychainRead::Found : (status == errSecItemNotFound ? KeychainRead::Missing : KeychainRead::Error);
    }
    bool add(const std::string& identity, const Key& key) {
        auto service = CFStringCreateWithCString(nullptr, identity.c_str(), kCFStringEncodingUTF8);
        auto data = CFDataCreate(nullptr, key.data(), key.size());
        const void* names[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData, kSecAttrAccessible};
        const void* values[] = {kSecClassGenericPassword, service, CFSTR("savedata_secret"), data, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly};
        auto item = CFDictionaryCreate(nullptr, names, values, 5, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        auto status = SecItemAdd(item, nullptr);
        CFRelease(item); CFRelease(data); CFRelease(service);
        if (status != errSecDuplicateItem) return status == errSecSuccess;
        Key existing{};
        const bool same = read(identity, existing) == KeychainRead::Found && existing == key;
        crypto_wipe(existing.data(), existing.size());
        return same;
    }
    template<class Accept>
    bool visitLegacy(Accept accept) {
        const void* names[] = {kSecClass, kSecAttrAccount, kSecReturnAttributes, kSecReturnData, kSecMatchLimit};
        const void* values[] = {kSecClassGenericPassword, CFSTR("savedata_secret"), kCFBooleanTrue, kCFBooleanTrue, kSecMatchLimitAll};
        auto query = CFDictionaryCreate(nullptr, names, values, 5, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFTypeRef result = nullptr;
        const auto status = SecItemCopyMatching(query, &result);
        CFRelease(query);
        bool found = false;
        if (status == errSecSuccess && result && CFGetTypeID(result) == CFArrayGetTypeID()) {
            auto items = (CFArrayRef)result;
            for (CFIndex i = 0; i < CFArrayGetCount(items) && !found; ++i) {
                auto raw = CFArrayGetValueAtIndex(items, i);
                if (!raw || CFGetTypeID(raw) != CFDictionaryGetTypeID()) continue;
                auto item = (CFDictionaryRef)raw;
                auto service = (CFStringRef)CFDictionaryGetValue(item, kSecAttrService);
                auto data = (CFDataRef)CFDictionaryGetValue(item, kSecValueData);
                if (!service || CFGetTypeID(service) != CFStringGetTypeID() ||
                    !data || CFGetTypeID(data) != CFDataGetTypeID() || CFDataGetLength(data) != 32) continue;
                std::vector<char> text(CFStringGetMaximumSizeForEncoding(CFStringGetLength(service), kCFStringEncodingUTF8) + 1);
                if (!CFStringGetCString(service, text.data(), text.size(), kCFStringEncodingUTF8)) continue;
                Key candidate{};
                CFDataGetBytes(data, CFRangeMake(0, 32), candidate.data());
                found = accept(text.data(), candidate);
                crypto_wipe(candidate.data(), candidate.size());
            }
        }
        if (result) CFRelease(result);
        return found;
    }
};
}
#endif
bool deviceKey(const std::filesystem::path& root,bool create,Key& key) {
    try {
#if PLATFORMER_IOS
        IosKeychain chain;
        return loadIosKey(Edition::storageName, create, key, chain, [&](const Key& candidate) {
            // Store performs the existing signature, AEAD and schema checks.
            // A provider avoids recursive Keychain access during this probe.
            Store probe(root, Edition::storageName, [&](bool, Key& out) { out = candidate; return true; });
            nlohmann::json value;
            return probe.load(value) == Store::Result::Loaded;
        });
#elif defined(__APPLE__)
        auto service=CFStringCreateWithCString(nullptr,("com.Benno111.savedata:"+root.u8string()).c_str(),kCFStringEncodingUTF8);
        const void* names[]={kSecClass,kSecAttrService,kSecAttrAccount,kSecReturnData,kSecMatchLimit};
        const void* values[]={kSecClassGenericPassword,service,CFSTR("savedata_secret"),kCFBooleanTrue,kSecMatchLimitOne};
        auto query=CFDictionaryCreate(nullptr,names,values,5,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        CFTypeRef result=nullptr; auto status=SecItemCopyMatching(query,&result);
        bool ok=false;
        if(status==errSecSuccess&&result&&CFGetTypeID(result)==CFDataGetTypeID()&&CFDataGetLength((CFDataRef)result)==32) {
            CFDataGetBytes((CFDataRef)result,CFRangeMake(0,32),key.data()); ok=true;
        } else if(status==errSecItemNotFound&&create&&randomBytes(key.data(),32)) {
            auto data=CFDataCreate(nullptr,key.data(),32);
            const void* addNames[]={kSecClass,kSecAttrService,kSecAttrAccount,kSecValueData,kSecAttrAccessible};
            const void* addValues[]={kSecClassGenericPassword,service,CFSTR("savedata_secret"),data,kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly};
            auto add=CFDictionaryCreate(nullptr,addNames,addValues,5,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
            ok=SecItemAdd(add,nullptr)==errSecSuccess; CFRelease(add); CFRelease(data);
        }
        if(result)CFRelease(result); CFRelease(query); CFRelease(service); return ok;
#else
        auto directory=root/".keys"; auto path=directory/"savedata_secret.key";
        std::vector<unsigned char> blob;
        if(std::filesystem::exists(path)) {
#if !defined(_WIN32) && !defined(__ANDROID__)
            int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return false;
            struct stat st{};
            bool ok=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&(st.st_mode&077)==0&&st.st_size==32;
            size_t pos=0;
            while(ok&&pos<32){auto n=::read(fd,key.data()+pos,32-pos);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}pos+=n;}
            close(fd);return ok;
#else
            if(std::filesystem::file_size(path)>4096)return false;
            std::ifstream in(path,std::ios::binary); if(!in)return false;
            blob.assign(std::istreambuf_iterator<char>(in),{});
#ifdef _WIN32
            DATA_BLOB encrypted{static_cast<DWORD>(blob.size()),blob.data()}, plain{};
            if(!CryptUnprotectData(&encrypted,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain))return false;
            bool ok=plain.cbData==32; if(ok)std::copy(plain.pbData,plain.pbData+32,key.begin());
            SecureZeroMemory(plain.pbData,plain.cbData); LocalFree(plain.pbData); return ok;
#elif defined(__ANDROID__)
            std::vector<unsigned char> plain;
            bool ok=wrap(false,blob,plain)&&plain.size()==32;
            if(ok)std::copy(plain.begin(),plain.end(),key.begin());
            crypto_wipe(plain.data(),plain.size()); return ok;
#endif
#endif
        }
        if(!create||!randomBytes(key.data(),32))return false;
        std::filesystem::create_directories(directory);
        if(!std::filesystem::is_directory(std::filesystem::symlink_status(directory)))return false;
#ifdef _WIN32
        DATA_BLOB plain{32,key.data()},encrypted{};
        if(!CryptProtectData(&plain,L"Dorfplatformer savedata",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted))return false;
        blob.assign(encrypted.pbData,encrypted.pbData+encrypted.cbData); LocalFree(encrypted.pbData);
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        DWORD written=0; bool ok=WriteFile(file,blob.data(),static_cast<DWORD>(blob.size()),&written,nullptr)&&written==blob.size();
        if(ok)ok=FlushFileBuffers(file)!=0; CloseHandle(file); return ok;
#else
#ifdef __ANDROID__
        std::vector<unsigned char> plain(key.begin(),key.end());
        bool wrapped=wrap(true,plain,blob);crypto_wipe(plain.data(),plain.size());
        if(!wrapped)return false;
#else
        blob.assign(key.begin(),key.end());
#endif
        if(chmod(directory.c_str(),0700))return false;
        int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd<0)return false;
        size_t pos=0; bool ok=true;
        while(pos<blob.size()) { auto n=write(fd,blob.data()+pos,blob.size()-pos); if(n<0&&errno==EINTR)continue; if(n<=0){ok=false;break;} pos+=n; }
        if(ok)ok=fsync(fd)==0; if(close(fd))ok=false;
        crypto_wipe(blob.data(),blob.size()); if(!ok)unlink(path.c_str());
        if(ok){int dir=open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dir>=0){(void)fsync(dir);close(dir);}}
        return ok;
#endif
#endif
    }catch(...){return false;}
}
}
