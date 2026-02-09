#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace celegans {

class Config {
public:
    Config() = default;

    void load_from_file(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open config file: " + path);
        }
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            values_[key] = val;
        }
    }

    void set(const std::string& key, const std::string& value) {
        values_[key] = value;
    }

    std::string get_string(const std::string& key, const std::string& default_val = "") const {
        auto it = values_.find(key);
        return (it != values_.end()) ? it->second : default_val;
    }

    double get_double(const std::string& key, double default_val = 0.0) const {
        auto it = values_.find(key);
        return (it != values_.end()) ? std::stod(it->second) : default_val;
    }

    int get_int(const std::string& key, int default_val = 0) const {
        auto it = values_.find(key);
        return (it != values_.end()) ? std::stoi(it->second) : default_val;
    }

    bool get_bool(const std::string& key, bool default_val = false) const {
        auto it = values_.find(key);
        if (it == values_.end()) return default_val;
        return (it->second == "true" || it->second == "1" || it->second == "yes");
    }

private:
    std::unordered_map<std::string, std::string> values_;

    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end = s.find_last_not_of(" \t\r\n");
        return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }
};

} // namespace celegans
