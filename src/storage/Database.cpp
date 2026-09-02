#include "storage/Database.hpp"
#include "core/Logger.hpp"

namespace LightweightMC::Storage
{

    Database::Database(std::string path) : m_path(std::move(path)) {}

    Database::~Database()
    {
        if (m_db)
            sqlite3_close(m_db);
    }

    bool Database::init()
    {
        if (sqlite3_open(m_path.c_str(), &m_db) != SQLITE_OK)
        {
            Core::Logger::error("Impossible d'ouvrir la base SQLite");
            return false;
        }

        // Activer le mode WAL pour la vitesse
        sqlite3_exec(m_db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);

        const char *sql = R"(
        CREATE TABLE IF NOT EXISTS books (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            book_uuid TEXT UNIQUE,
            title TEXT,
            author TEXT,
            pages_json TEXT
        );
        CREATE TABLE IF NOT EXISTS block_entities (
            chunk_x INTEGER, chunk_z INTEGER,
            rel_x INTEGER, y INTEGER, rel_z INTEGER,
            type INTEGER, data_blob BLOB,
            PRIMARY KEY (chunk_x, chunk_z, rel_x, y, rel_z)
        );
    )";

        return sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    }

} // namespace LightweightMC::Storage