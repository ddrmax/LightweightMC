#pragma once
#include <string>
#include <sqlite3.h>

namespace LightweightMC::Storage {

class Database {
private:
    sqlite3* m_db{nullptr};
    std::string m_path;

public:
    explicit Database(std::string path);
    ~Database();

    bool init();
};

} // namespace LightweightMC::Storage