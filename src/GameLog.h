#pragma once
#include <SDL3/SDL.h>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

class GameLog {
public:
    GameLog(const std::string& root, bool enabled) : path_((std::filesystem::u8path(root) / "logs/game.log").u8string()) {
        SDL_GetLogOutputFunction(&previous_, &previousContext_);
        SDL_SetLogOutputFunction(&write, this); setEnabled(enabled);
    }
    ~GameLog() { SDL_SetLogOutputFunction(previous_, previousContext_); }
    GameLog(const GameLog&) = delete;
    GameLog& operator=(const GameLog&) = delete;
    void setEnabled(bool enabled) {
        { std::lock_guard<std::mutex> lock(mutex_);
          enabled_ = enabled;
          if (!enabled) file_.close();
          else if (!file_.is_open()) open(); }
        SDL_SetLogPriorities(enabled ? SDL_LOG_PRIORITY_INFO : SDL_LOG_PRIORITY_CRITICAL);
    }
    std::vector<std::string> lines() const { std::lock_guard<std::mutex> lock(mutex_); return {recent_.begin(), recent_.end()}; }
    const std::string& path() const { return path_; }
private:
    void open() {
        std::error_code ec; std::filesystem::create_directories(std::filesystem::u8path(path_).parent_path(), ec);
        bytes_ = std::filesystem::file_size(std::filesystem::u8path(path_), ec); if (ec) bytes_ = 0;
        file_.open(std::filesystem::u8path(path_), std::ios::app | std::ios::binary);
    }
    static void SDLCALL write(void* context, int category, SDL_LogPriority priority, const char* message) {
        auto& self = *static_cast<GameLog*>(context);
        { std::lock_guard<std::mutex> lock(self.mutex_);
          if (!self.enabled_) return;
          const std::string text = "[" + std::to_string(SDL_GetTicks()) + "][" + std::to_string(category) +
              ":" + std::to_string(static_cast<int>(priority)) + "] " + std::string(message ? message : "").substr(0, 8192);
          self.recent_.push_back(text); if (self.recent_.size() > 200) self.recent_.pop_front();
          if (self.bytes_ + text.size() > 8 * 1024 * 1024) {
              self.file_.close(); std::error_code ec;
              auto backup = std::filesystem::u8path(self.path_); backup += ".previous";
              std::filesystem::remove(backup, ec); std::filesystem::rename(std::filesystem::u8path(self.path_), backup, ec);
              self.open();
          }
          if (self.file_) { self.file_ << text << '\n'; self.file_.flush(); self.bytes_ += text.size() + 1; } }
        if (self.previous_) self.previous_(self.previousContext_, category, priority, message);
    }
    std::string path_;
    mutable std::mutex mutex_;
    std::deque<std::string> recent_;
    std::ofstream file_;
    uintmax_t bytes_ = 0;
    bool enabled_ = false;
    SDL_LogOutputFunction previous_ = nullptr;
    void* previousContext_ = nullptr;
};
