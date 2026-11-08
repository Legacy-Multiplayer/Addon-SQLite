#pragma once

#include "SDK/Table.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct sqlite3;

namespace Database
{
constexpr size_t MaximumPayload = 256 * 1024;
constexpr size_t MaximumResult = 1024 * 1024;

enum class Operation
{
    Execute,
    Query,
    Transaction,
    Close
};

struct Statement
{
    std::string sql;
    std::vector<SDK::CValue> parameters;
};

struct Request
{
    Operation operation;
    std::vector<Statement> statements;
};

struct Result
{
    bool success = false;
    SDK::CValue data;
    std::string error;
};

std::filesystem::path ResolvePath(const std::filesystem::path& root, const std::string& relative);

class Connection
{
public:
    explicit Connection(std::filesystem::path path);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Result Run(const Request& request, const std::function<bool()>& cancelled);

private:
    void Open();
    std::filesystem::path m_path;
    sqlite3* m_database = nullptr;
};
} // namespace Database
