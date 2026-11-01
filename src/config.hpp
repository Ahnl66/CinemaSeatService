#pragma once
#include "wire.hpp"
namespace csad {
struct Config {
    Json value;
    static Config defaults();
    static Config read(const std::string& path);
    static Config parse(const Json& input);
    int get(const char* group, const char* key) const { return value.at(group).at(key).get<int>(); }
};
}
