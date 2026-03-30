#pragma once

#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <optional>
#include <mutex>
#include <thread>
#include <syncstream>  
#include <ctime>      

using namespace std::literals;

#define LOG(...) Logger::GetInstance().Log(__VA_ARGS__)

class Logger {
    auto GetTime() const {
        std::lock_guard<std::mutex> lock(ts_mutex_);
        if (manual_ts_) {
            return *manual_ts_;
        }
        return std::chrono::system_clock::now();
    }

    std::string GetTimeStamp() const {
        const auto now = GetTime();
        const auto t_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&t_c), "%F %T");
        return ss.str();
    }

    // Для имени файла возьмите дату с форматом "%Y_%m_%d"
    std::string GetFileTimeStamp() const {
        const auto now = GetTime();
        const auto t_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&t_c), "%Y_%m_%d");
        return ss.str();
    }

    void CheckAndRotateFile() {
        auto new_file_stamp = GetFileTimeStamp();
        if (current_file_stamp_ != new_file_stamp) {
            if (log_file_.is_open()) {
                log_file_.close();
            }
            // Открываем новый файл
            std::string filename = "/var/log/sample_log_" + new_file_stamp + ".log";
            log_file_.open(filename, std::ios::app);
            current_file_stamp_ = new_file_stamp;
        }
    }

    Logger() : current_file_stamp_("") {
        CheckAndRotateFile();
    }
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

public:
    static Logger& GetInstance() {
        static Logger obj;
        return obj;
    }

    // Выведите в поток все аргументы.
    template<class... Ts>
    void Log(const Ts&... args) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        
        CheckAndRotateFile();
        
        if (!log_file_.is_open()) {
            return; 
        }
        
        std::osyncstream sync_stream(log_file_);
        sync_stream << GetTimeStamp() << ": ";
        ((sync_stream << args), ...);
        sync_stream << std::endl;
    }

    // Установите manual_ts_. Учтите, что эта операция может выполняться
    // параллельно с выводом в поток, вам нужно предусмотреть 
    // синхронизацию.
    void SetTimestamp(std::chrono::system_clock::time_point ts) {
        std::lock_guard<std::mutex> lock(ts_mutex_);
        manual_ts_ = ts;
    }

private:
    std::optional<std::chrono::system_clock::time_point> manual_ts_;
    mutable std::mutex ts_mutex_; 
    std::mutex log_mutex_;  
    std::ofstream log_file_;  
    std::string current_file_stamp_;  
};