#include "SecretSave.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include <fstream>
#include <vector>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
namespace SecretData {
namespace {
constexpr size_t limit = 1024 * 1024;
constexpr char magic[] = "DFSEC001";
std::string read(const std::filesystem::path& p) {
    if (std::filesystem::file_size(p) > limit) throw std::runtime_error("Secret save too large");
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read secret save");
    std::string data((std::istreambuf_iterator<char>(in)), {});
    if (in.bad()) throw std::runtime_error("Cannot read secret save");
    return data;
}
bool replace(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::array<uint8_t,16> random{};
    if(!randomBytes(random.data(),random.size()))return false;
    constexpr char hex[]="0123456789abcdef";std::string suffix=".tmp-";
    for(auto c:random){suffix+=hex[c>>4];suffix+=hex[c&15];}
    auto temp = path; temp += suffix;
#ifdef _WIN32
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;
    bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
    if(ok)ok=FlushFileBuffers(file)!=0;
    if(!CloseHandle(file))ok=false;
    if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok)DeleteFileW(temp.c_str());
    return ok;
#else
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    bool ok = fchmod(fd, 0600) == 0;
    size_t pos = 0;
    while (ok && pos < bytes.size()) {
        auto n = write(fd, bytes.data() + pos, bytes.size() - pos);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; } pos += n;
    }
    if (ok) ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (ok) ok = rename(temp.c_str(), path.c_str()) == 0;
    if (!ok) unlink(temp.c_str());
    if(ok) {
        int directory=open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        if(directory>=0){(void)fsync(directory);close(directory);}
    }
    return ok;
#endif
}
}
Store::Store(std::filesystem::path root, std::string context, KeyProvider provider)
    : root_(std::move(root)), path_(root_ / "savedata_secret"), context_(std::move(context)), provider_(std::move(provider)) {}
Store::~Store() { crypto_wipe(encryption_.data(), encryption_.size()); crypto_wipe(signing_.data(), signing_.size()); }
bool Store::keys(bool create) {
    if (ready_) return true;
    Key master{}, seed{};
    bool ok = provider_ ? provider_(create, master) : deviceKey(root_, create, master);
    if (ok) {
        auto domain = context_ + ":encryption:v1";
        crypto_blake2b_keyed(encryption_.data(),32,master.data(),32,reinterpret_cast<const uint8_t*>(domain.data()),domain.size());
        domain = context_ + ":signature:v1";
        crypto_blake2b_keyed(seed.data(),32,master.data(),32,reinterpret_cast<const uint8_t*>(domain.data()),domain.size());
        crypto_ed25519_key_pair(signing_.data(),public_.data(),seed.data());
    }
    crypto_wipe(master.data(),32); crypto_wipe(seed.data(),32);
    return ready_ = ok;
}
bool Store::decode(const std::string& bytes, nlohmann::json& value) {
    if (bytes.size() < 112 || std::memcmp(bytes.data(), magic, 8) != 0) return false;
    auto p = reinterpret_cast<const uint8_t*>(bytes.data());
    size_t signedSize = bytes.size()-64, size = bytes.size()-112;
    if (crypto_ed25519_check(p+signedSize,public_.data(),p,signedSize)) return false;
    std::string plain(size,'\0');
    int failed = crypto_aead_unlock(reinterpret_cast<uint8_t*>(plain.data()),p+32,encryption_.data(),p+8,
        reinterpret_cast<const uint8_t*>(context_.data()),context_.size(),p+48,size);
    bool ok = false;
    if (!failed) {
        try { value = nlohmann::json::parse(plain); ok = value.is_object() && value.value("version",0)==1
            && value.contains("achievements") && value["achievements"].is_object()
            && value.contains("account") && value["account"].is_object()
            && value["account"].value("server_url",nlohmann::json()).is_string()
            && value["account"].value("username",nlohmann::json()).is_string()
            && value["account"].value("auth_token",nlohmann::json()).is_string(); } catch (...) {}
    }
    crypto_wipe(plain.data(),plain.size());
    return ok;
}
Store::Result Store::load(nlohmann::json& value) {
    try {
        const auto status=std::filesystem::symlink_status(path_);
        if(status.type()==std::filesystem::file_type::not_found)return Result::Missing;
        if(!std::filesystem::is_regular_file(status)){blocked_=true;return Result::Error;}
        if (!keys(false) || !decode(read(path_),value)) { blocked_=true; return Result::Error; }
        return Result::Loaded;
    } catch (...) { blocked_=true; return Result::Error; }
}
bool Store::save(const nlohmann::json& value) {
    if (blocked_) return false;
    try {
        const auto status=std::filesystem::symlink_status(path_);
        bool exists=status.type()!=std::filesystem::file_type::not_found;
        if(exists&&!std::filesystem::is_regular_file(status)){blocked_=true;return false;}
        if (!keys(!exists)) return false;
        nlohmann::json old;
        if (exists && !decode(read(path_),old)) { blocked_=true; return false; }
        std::string plain = value.dump();
        if (plain.size() > limit-112) { crypto_wipe(plain.data(),plain.size()); return false; }
        std::vector<uint8_t> bytes(plain.size()+112);
        std::memcpy(bytes.data(),magic,8);
        if (!randomBytes(bytes.data()+8,24)) { crypto_wipe(plain.data(),plain.size()); return false; }
        crypto_aead_lock(bytes.data()+48,bytes.data()+32,encryption_.data(),bytes.data()+8,
            reinterpret_cast<const uint8_t*>(context_.data()),context_.size(),reinterpret_cast<const uint8_t*>(plain.data()),plain.size());
        crypto_wipe(plain.data(),plain.size());
        crypto_ed25519_sign(bytes.data()+bytes.size()-64,signing_.data(),bytes.data(),bytes.size()-64);
        std::filesystem::create_directories(root_);
        return replace(path_,bytes);
    } catch (...) { return false; }
}
void scrubLegacy(nlohmann::json& j) {
    if (!j.is_object()) return;
    j.erase("achievements");
    for (auto k : {"level_server_auth_token","level_api_auth_token","level_server_account_username","level_api_account_username"}) j.erase(k);
    if (j.contains("settings") && j["settings"].is_object() && j["settings"].contains("network")) {
        auto& n=j["settings"]["network"];
        if (n.is_object()) for (auto k : {"level_server_auth_token","level_api_auth_token","level_server_account_username","level_api_account_username"}) n.erase(k);
    }
}
}
