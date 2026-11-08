#include "SDK/Plugin.h"
#include "Database.h"

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>

using namespace SDK;

class CSQLite final : public CPlugin
{
    struct Request
    {
        int64_t id;
        Database::Request operation;
        std::optional<CCallback> callback;
    };

    struct Session
    {
        explicit Session(std::filesystem::path location) : path(std::move(location)), database(path)
        {
        }

        std::filesystem::path path;
        Database::Connection database;
        std::deque<Request> queue;
        std::atomic<bool> cancelled{false};
        bool busy = false;
        bool closing = false;
    };

    bool OnLoad() override
    {
        m_root = std::filesystem::current_path() / "resources";
        Function("openDatabase", [this](std::string path)
        {
            try
            {
                if (m_sessions.size() >= 16)
                {
                    throw std::runtime_error("At most 16 database handles may be open.");
                }
                const auto resolved = Database::ResolvePath(m_root, path);
                for (const auto& entry : m_sessions)
                {
#ifdef _WIN32
                    bool same = _wcsicmp(entry.second->path.c_str(), resolved.c_str()) == 0;
#else
                    bool same = entry.second->path == resolved;
#endif
                    if (!same && std::filesystem::exists(resolved) && std::filesystem::exists(entry.second->path))
                    {
                        same = std::filesystem::equivalent(resolved, entry.second->path);
                    }

                    if (same)
                    {
                        throw std::runtime_error("This database already has an open handle.");
                    }
                }
                const int64_t id = Next(m_nextDatabase);
                m_sessions.emplace(id, std::make_shared<Session>(resolved));
                return std::make_tuple(std::optional<int64_t>(id), std::string());
            }
            catch (const std::exception& error)
            {
                return std::make_tuple(std::optional<int64_t>{}, std::string(error.what()));
            }
        });
        Function("closeDatabase", [this](int64_t id, std::optional<CCallback> callback)
        {
            auto [request, error] = Enqueue(id, {Database::Operation::Close, {}}, std::move(callback));
            return std::make_tuple(request > 0, error);
        });
        Function("executeDatabase", [this](int64_t id, std::string sql, std::optional<CTable> parameters, std::optional<CCallback> callback)
        {
            return Submit(id, Database::Operation::Execute, std::move(sql), parameters.value_or(CTable{}), std::move(callback));
        });
        Function("queryDatabase", [this](int64_t id, std::string sql, std::optional<CTable> parameters, std::optional<CCallback> callback)
        {
            return Submit(id, Database::Operation::Query, std::move(sql), parameters.value_or(CTable{}), std::move(callback));
        });
        Function("transactionDatabase", [this](int64_t id, CTable statements, std::optional<CCallback> callback)
        {
            if (statements.Size() < 1 || statements.Size() > 128)
            {
                throw std::invalid_argument("Transactions require 1 to 128 statements.");
            }
            Database::Request operation{Database::Operation::Transaction, {}};
            size_t bytes = 0;
            for (size_t index = 1; index <= statements.Size(); ++index)
            {
                const auto statement = statements.Get<CTable>(static_cast<int64_t>(index));
                for (const auto& entry : statement.GetEntries())
                {
                    const auto* key = std::get_if<std::string>(&entry.first);
                    if (!key || (*key != "sql" && *key != "parameters"))
                    {
                        throw std::invalid_argument("Statement fields are sql and parameters.");
                    }
                }
                operation.statements.push_back(Statement(statement.Get<std::string>("sql"), statement.Get<CTable>("parameters", CTable{}), bytes));
            }
            return Result(Enqueue(id, std::move(operation), std::move(callback)));
        });
        Every(10, [this]()
        {
            for (const auto& entry : m_sessions)
            {
                Pump(entry.first, entry.second);
            }
        });
        Event("onMainScriptExit", [this]()
        {
            Reset();
        });
        return true;
    }

    void OnUnload() override
    {
        Reset();
    }

    static int64_t Next(int64_t& counter)
    {
        if (counter == std::numeric_limits<int64_t>::max())
        {
            throw std::runtime_error("Database handle space exhausted.");
        }
        return ++counter;
    }

    static Database::Statement Statement(std::string sql, const CTable& parameters, size_t& bytes)
    {
        if (sql.empty() || sql.size() > 64 * 1024 || sql.find('\0') != std::string::npos)
        {
            throw std::invalid_argument("Invalid SQL text.");
        }
        bytes += sql.size();
        int64_t count = 0;
        const bool packed = parameters.Contains("n");
        if (packed)
        {
            count = parameters.Get<int64_t>("n");
        }
        if (count < 0 || count > 256)
        {
            throw std::invalid_argument("At most 256 parameters are allowed.");
        }
        for (const auto& entry : parameters.GetEntries())
        {
            if (const auto* index = std::get_if<int64_t>(&entry.first))
            {
                if (*index < 1 || *index > 256 || (packed && *index > count))
                {
                    throw std::invalid_argument("Invalid parameter index.");
                }
                if (!packed)
                {
                    count = std::max(count, *index);
                }
            }
            else if (std::get<std::string>(entry.first) != "n")
            {
                throw std::invalid_argument("Parameters must be an array or table.pack result.");
            }
        }
        Database::Statement result{std::move(sql), {}};
        for (int64_t index = 1; index <= count; ++index)
        {
            if (!parameters.Contains(index) && !packed)
            {
                throw std::invalid_argument("Use table.pack(...) to bind NULL parameters.");
            }
            auto value = parameters.Get<CValue>(index, CValue{});
            if (std::holds_alternative<std::shared_ptr<const CTable>>(value.GetData()))
            {
                throw std::invalid_argument("Parameters must be scalars or NULL.");
            }
            if (const auto* text = std::get_if<std::string>(&value.GetData()))
            {
                bytes += text->size();
            }
            if (bytes > Database::MaximumPayload)
            {
                throw std::length_error("Database request exceeds 256 KiB.");
            }
            result.parameters.push_back(std::move(value));
        }
        if (bytes > Database::MaximumPayload)
        {
            throw std::length_error("Database request exceeds 256 KiB.");
        }
        return result;
    }

    static std::tuple<CValue, std::string> Result(std::tuple<int64_t, std::string> result)
    {
        auto [id, error] = std::move(result);
        return {id ? CValue(id) : CValue(false), std::move(error)};
    }

    std::tuple<CValue, std::string> Submit(int64_t id, Database::Operation kind, std::string sql, const CTable& parameters, std::optional<CCallback> callback)
    {
        size_t bytes = 0;
        Database::Request operation{kind, {Statement(std::move(sql), parameters, bytes)}};
        return Result(Enqueue(id, std::move(operation), std::move(callback)));
    }

    std::tuple<int64_t, std::string> Enqueue(int64_t id, Database::Request operation, std::optional<CCallback> callback)
    {
        const auto found = m_sessions.find(id);
        if (found == m_sessions.end() || found->second->closing)
        {
            return {0, "Database handle is invalid or closing."};
        }
        if (m_pending >= 128)
        {
            return {0, "Database queue is full."};
        }
        const bool close = operation.operation == Database::Operation::Close;
        const auto request = Next(m_nextRequest);
        found->second->queue.push_back({request, std::move(operation), std::move(callback)});
        found->second->closing = close;
        ++m_pending;
        return {request, {}};
    }

    void Pump(int64_t id, const std::shared_ptr<Session>& session)
    {
        if (session->busy || session->queue.empty())
        {
            return;
        }
        const auto request = session->queue.front();
        const auto epoch = m_epoch;
        try
        {
            Async([session, operation = request.operation](CCancellation cancellation)
            {
                return session->database.Run(operation, [session, cancellation]()
                {
                    return session->cancelled.load(std::memory_order_relaxed) || cancellation.IsCancelled();
                });
            }, [this, session, request, epoch, id](Database::Result result)
            {
                if (epoch != m_epoch)
                {
                    return;
                }
                session->busy = false;
                --m_pending;
                if (request.operation.operation == Database::Operation::Close)
                {
                    m_sessions.erase(id);
                }
                if (request.callback)
                {
                    if (request.callback->IsValid())
                    {
                        request.callback->Invoke(result.success, result.data, result.success ? std::optional<std::string>{} : result.error);
                    }
                }
                else if (!result.success)
                {
                    Log.Error("{}", result.error);
                }
            });
            session->busy = true;
            session->queue.pop_front();
        }
        catch (const std::runtime_error&)
        {
            // Shared worker capacity: retry next tick.
        }
    }

    void Reset()
    {
        ++m_epoch;
        for (const auto& entry : m_sessions)
        {
            entry.second->cancelled.store(true, std::memory_order_relaxed);
            entry.second->queue.clear();
        }
        m_sessions.clear();
        m_pending = 0;
    }

    std::filesystem::path m_root;
    std::map<int64_t, std::shared_ptr<Session>> m_sessions;
    int64_t m_nextDatabase = 0;
    int64_t m_nextRequest = 0;
    uint64_t m_epoch = 0;
    size_t m_pending = 0;
};

REGISTER_PLUGIN(CSQLite)
