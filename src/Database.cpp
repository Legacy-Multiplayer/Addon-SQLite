#include "Database.h"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>

namespace Database
{
using SDK::CTable;
using SDK::CValue;
using Prepared = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

static void Check(int status, sqlite3* database)
{
    if (status != SQLITE_OK)
    {
        throw std::runtime_error(database ? sqlite3_errmsg(database) : sqlite3_errstr(status));
    }
}

std::filesystem::path ResolvePath(const std::filesystem::path& root, const std::string& relative)
{
    if (relative.empty() || relative.size() > 240 || relative.find('\0') != std::string::npos ||
        relative.find(':') != std::string::npos || relative.find('\\') != std::string::npos)
    {
        throw std::invalid_argument("Use a resource-relative path with forward slashes.");
    }

    const auto path = std::filesystem::u8path(relative);
    if (path.is_absolute() || path.has_root_name())
    {
        throw std::invalid_argument("Absolute database paths are not allowed.");
    }

    for (const auto& part : path)
    {
        const auto name = part.u8string();
        if (name == ".." || name == "." || name.empty() || name.back() == '.' || name.back() == ' ')
        {
            throw std::invalid_argument("Invalid database path component.");
        }
    }

    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value)
    {
        return static_cast<char>(std::tolower(value));
    });

    if (extension != ".sqlite" && extension != ".sqlite3" && extension != ".db")
    {
        throw std::invalid_argument("Database files require .sqlite, .sqlite3 or .db.");
    }

    std::filesystem::create_directories(root);
    const auto base = std::filesystem::canonical(root);
    auto current = base;
    for (const auto& part : path)
    {
        current /= part;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(current)))
        {
            throw std::invalid_argument("Database paths cannot contain symbolic links.");
        }
    }

    const auto resolved = std::filesystem::weakly_canonical(base / path);
    auto baseIterator = base.begin();
    auto resolvedIterator = resolved.begin();
    for (; baseIterator != base.end(); ++baseIterator, ++resolvedIterator)
    {
        if (resolvedIterator == resolved.end() || *baseIterator != *resolvedIterator)
        {
            throw std::invalid_argument("Database path escapes resources.");
        }
    }

    std::filesystem::create_directories(resolved.parent_path());
    return resolved;
}

Connection::Connection(std::filesystem::path path) : m_path(std::move(path))
{
}

Connection::~Connection()
{
    if (m_database)
    {
        sqlite3_close(m_database);
    }
}

static int Authorize(void*, int action, const char*, const char*, const char*, const char*)
{
    switch (action)
    {
    case SQLITE_ATTACH:
    case SQLITE_DETACH:
    case SQLITE_PRAGMA:
    case SQLITE_TRANSACTION:
    case SQLITE_SAVEPOINT:
    case SQLITE_CREATE_VTABLE:
    case SQLITE_DROP_VTABLE:
        return SQLITE_DENY;

    default:
        return SQLITE_OK;
    }
}

void Connection::Open()
{
    if (m_database)
    {
        return;
    }

    const int status = sqlite3_open_v2(m_path.u8string().c_str(), &m_database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_PRIVATECACHE, nullptr);

    if (status != SQLITE_OK)
    {
        const std::string message = m_database ? sqlite3_errmsg(m_database) : sqlite3_errstr(status);
        sqlite3_close(m_database);
        m_database = nullptr;
        throw std::runtime_error(message);
    }

    struct CloseOnFailure
    {
        sqlite3*& database;
        bool ready = false;

        ~CloseOnFailure()
        {
            if (!ready)
            {
                sqlite3_close(database);
                database = nullptr;
            }
        }
    } cleanup{m_database};

    Check(sqlite3_busy_timeout(m_database, 250), m_database);
    Check(sqlite3_exec(m_database, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA max_page_count=262144;", nullptr, nullptr, nullptr), m_database);
    Check(sqlite3_db_config(m_database, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr), m_database);
    Check(sqlite3_db_config(m_database, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr), m_database);
    sqlite3_limit(m_database, SQLITE_LIMIT_LENGTH, 1024 * 1024);
    sqlite3_limit(m_database, SQLITE_LIMIT_SQL_LENGTH, 64 * 1024);
    sqlite3_limit(m_database, SQLITE_LIMIT_COLUMN, 64);
    sqlite3_limit(m_database, SQLITE_LIMIT_VARIABLE_NUMBER, 256);
    sqlite3_limit(m_database, SQLITE_LIMIT_ATTACHED, 0);
    Check(sqlite3_set_authorizer(m_database, Authorize, nullptr), m_database);
    cleanup.ready = true;
}

static Prepared Prepare(sqlite3* database, const Statement& operation, bool query)
{
    const auto& sql = operation.sql;
    if (sql.empty() || sql.size() > 64 * 1024 || sql.find('\0') != std::string::npos)
    {
        throw std::invalid_argument("Invalid SQL length or embedded NUL.");
    }

    sqlite3_stmt* raw = nullptr;
    const char* tail = nullptr;
    const int status = sqlite3_prepare_v2(database, sql.data(), static_cast<int>(sql.size()), &raw, &tail);
    Prepared statement(raw, sqlite3_finalize);
    Check(status, database);
    if (!statement)
    {
        throw std::invalid_argument("SQL contains no statement.");
    }

    while (tail < sql.data() + sql.size())
    {
        sqlite3_stmt* extraRaw = nullptr;
        const char* next = nullptr;
        const int extraStatus = sqlite3_prepare_v2(database, tail, static_cast<int>(sql.data() + sql.size() - tail), &extraRaw, &next);
        Prepared extra(extraRaw, sqlite3_finalize);
        Check(extraStatus, database);
        if (extra)
        {
            throw std::invalid_argument("Use one SQL statement per operation.");
        }
        if (!next || next <= tail)
        {
            break;
        }
        tail = next;
    }
    if (query && !sqlite3_stmt_readonly(statement.get()))
    {
        throw std::invalid_argument("queryDatabase accepts read-only statements.");
    }
    if (!query && sqlite3_column_count(statement.get()) != 0)
    {
        throw std::invalid_argument("Use queryDatabase to read rows; RETURNING is not supported.");
    }
    if (sqlite3_bind_parameter_count(statement.get()) != static_cast<int>(operation.parameters.size()))
    {
        throw std::invalid_argument("Parameter count does not match placeholders.");
    }

    for (size_t index = 0; index < operation.parameters.size(); ++index)
    {
        const auto& value = operation.parameters[index].GetData();
        const int slot = static_cast<int>(index + 1);
        const int bound = std::visit([slot, &statement](const auto& item) -> int
        {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                return sqlite3_bind_null(statement.get(), slot);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                return sqlite3_bind_int(statement.get(), slot, item ? 1 : 0);
            }
            else if constexpr (std::is_same_v<T, int64_t>)
            {
                return sqlite3_bind_int64(statement.get(), slot, item);
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return sqlite3_bind_double(statement.get(), slot, item);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                return sqlite3_bind_text(statement.get(), slot, item.data(), static_cast<int>(item.size()), SQLITE_TRANSIENT);
            }
            else
            {
                throw std::invalid_argument("Parameters must be scalars or NULL.");
            }
        }, value);
        Check(bound, database);
    }
    return statement;
}

struct Progress
{
    const std::function<bool()>& cancelled;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
};

static int Interrupt(void* data)
{
    const auto& progress = *static_cast<Progress*>(data);
    return progress.cancelled() || std::chrono::steady_clock::now() >= progress.deadline;
}

static CTable Execute(sqlite3* database, const Statement& operation)
{
    auto statement = Prepare(database, operation, false);
    const auto before = sqlite3_total_changes64(database);
    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(sqlite3_errmsg(database));
    }
    CTable result;
    result.Set("changes", sqlite3_total_changes64(database) - before);
    result.Set("lastInsertRowId", sqlite3_last_insert_rowid(database));
    return result;
}

static CTable Query(sqlite3* database, const Statement& operation)
{
    auto statement = Prepare(database, operation, true);
    const int columns = sqlite3_column_count(statement.get());
    std::vector<std::string> names;
    std::set<std::string> unique;
    for (int column = 0; column < columns; ++column)
    {
        names.emplace_back(sqlite3_column_name(statement.get(), column));
        if (!unique.insert(names.back()).second)
        {
            throw std::invalid_argument("Duplicate column names; use SQL AS aliases.");
        }
    }
    CTable rows;
    size_t rowCount = 0, cells = 0, bytes = 0;
    int status;
    while ((status = sqlite3_step(statement.get())) == SQLITE_ROW)
    {
        if (++rowCount > 1024 || cells + columns > 1024)
        {
            throw std::runtime_error("Query exceeds 1024 rows or cells; use pagination.");
        }
        CTable row;
        for (int column = 0; column < columns; ++column)
        {
            const auto& name = names[column];
            bytes += name.size() + 8;
            if (bytes > MaximumResult)
            {
                throw std::runtime_error("Query exceeds 1 MiB; use pagination.");
            }
            ++cells;
            CValue value;
            switch (sqlite3_column_type(statement.get(), column))
            {
            case SQLITE_NULL:
                break;
            case SQLITE_INTEGER:
                value = CValue(sqlite3_column_int64(statement.get(), column));
                break;
            case SQLITE_FLOAT:
                value = CValue(sqlite3_column_double(statement.get(), column));
                break;
            case SQLITE_TEXT:
            {
                const size_t size = sqlite3_column_bytes(statement.get(), column);
                if (size > MaximumResult - bytes)
                {
                    throw std::runtime_error("Query exceeds 1 MiB; use pagination.");
                }
                bytes += size;
                const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), column));
                value = CValue(std::string(data ? data : "", size));
                break;
            }
            default:
                throw std::runtime_error("BLOB results require SQL hex(column).");
            }
            row.Set(name, std::move(value));
        }
        rows.Set(static_cast<int64_t>(rowCount), row);
    }
    if (status != SQLITE_DONE)
    {
        throw std::runtime_error(sqlite3_errmsg(database));
    }
    return rows;
}

Result Connection::Run(const Request& request, const std::function<bool()>& cancelled)
{
    bool transaction = false;
    try
    {
        if (cancelled())
        {
            throw std::runtime_error("Database operation cancelled.");
        }
        if (request.operation == Operation::Close)
        {
            if (m_database)
            {
                Check(sqlite3_close(m_database), m_database);
                m_database = nullptr;
            }
            return {true, CTable{}, {}};
        }
        Open();
        Progress progress{cancelled};
        sqlite3_progress_handler(m_database, 1000, Interrupt, &progress);

        struct ClearProgress
        {
            sqlite3* database;

            ~ClearProgress()
            {
                sqlite3_progress_handler(database, 0, nullptr, nullptr);
            }
        } cleanup{m_database};

        if (request.operation == Operation::Query)
        {
            if (request.statements.size() != 1)
            {
                throw std::invalid_argument("Expected one query.");
            }
            return {true, Query(m_database, request.statements[0]), {}};
        }
        if (request.operation == Operation::Execute)
        {
            if (request.statements.size() != 1)
            {
                throw std::invalid_argument("Expected one statement.");
            }
            return {true, Execute(m_database, request.statements[0]), {}};
        }
        if (request.operation != Operation::Transaction || request.statements.empty() || request.statements.size() > 128)
        {
            throw std::invalid_argument("Transactions require 1 to 128 statements.");
        }
        sqlite3_set_authorizer(m_database, nullptr, nullptr);
        const int status = sqlite3_exec(m_database, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
        sqlite3_set_authorizer(m_database, Authorize, nullptr);
        Check(status, m_database);
        transaction = true;
        CTable result;
        int64_t index = 1;
        for (const auto& statement : request.statements)
        {
            if (cancelled())
            {
                throw std::runtime_error("Transaction cancelled.");
            }
            result.Set(index++, Execute(m_database, statement));
        }
        sqlite3_set_authorizer(m_database, nullptr, nullptr);
        const int committed = sqlite3_exec(m_database, "COMMIT", nullptr, nullptr, nullptr);
        sqlite3_set_authorizer(m_database, Authorize, nullptr);
        Check(committed, m_database);
        transaction = false;
        return {true, result, {}};
    }
    catch (const std::exception& error)
    {
        if (transaction)
        {
            sqlite3_progress_handler(m_database, 0, nullptr, nullptr);
            sqlite3_set_authorizer(m_database, nullptr, nullptr);
            sqlite3_exec(m_database, "ROLLBACK", nullptr, nullptr, nullptr);
            sqlite3_set_authorizer(m_database, Authorize, nullptr);
        }
        return {false, {}, error.what()};
    }
}
} // namespace Database
