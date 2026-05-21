#pragma once

// Thin wrapper over nanodbc for talking to SQL Server. Single-statement
// per connection. Errors surface as LiveConnectError with the joined
// SQLSTATE + diagnostic chain that nanodbc forwards out of unixODBC.

#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace liveconnect {

class LiveConnectError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class LiveAuth {
    SqlLogin,        // user + password supplied
    Integrated,      // Trusted_Connection=yes (Windows or kinit'd Linux)
};

struct LiveConnectParams {
    std::string server;      // e.g. "prodsql2-22" or "10.0.0.4,1433"
    std::string database;    // optional, empty = server default
    LiveAuth    auth = LiveAuth::SqlLogin;
    std::string user;        // SqlLogin only
    std::string password;    // SqlLogin only
    bool        encrypt = true;
    bool        trust_server_certificate = false;
    int         login_timeout_s = 15;
};

class LiveConnection {
public:
    LiveConnection();
    ~LiveConnection();
    LiveConnection(const LiveConnection&) = delete;
    LiveConnection& operator=(const LiveConnection&) = delete;
    LiveConnection(LiveConnection&&) noexcept;
    LiveConnection& operator=(LiveConnection&&) noexcept;

    void connect(const LiveConnectParams& p);
    void disconnect();
    bool is_connected() const;

    // Run statements that return nothing (CREATE/ALTER EVENT SESSION).
    void exec(const std::string& sql);

    // Run a query returning a single result row with a single text/xml
    // column. Used to slurp the ring_buffer target_data XML. Returns an
    // empty string when the query returns zero rows.
    std::string exec_scalar_text(const std::string& sql);

    // Run a script and return every text/xml column value across every
    // result set as a flat vector, in result-set + row + column order.
    // Used to harvest <ShowPlanXML> blobs out of SHOWPLAN_XML /
    // STATISTICS XML output, which SQL Server emits as a single text
    // column per plan row. Returns empty when nothing came back.
    //
    // Throws LiveConnectError on driver / server error. The exception
    // is also raised when cancel() was issued mid-flight.
    std::vector<std::string> exec_text_rows(const std::string& sql);

    // One row-set returned by a statement. column_names is fixed size
    // == columns; rows is rows × columns flat in row-major order. NULL
    // cells are stored as the empty string (we don't carry tri-state
    // null/non-null through the result panel today). Used by the
    // capture path that runs SET STATISTICS XML ON and pulls both
    // data rowsets AND embedded plan XML.
    struct ResultSet {
        std::vector<std::string> column_names;
        std::vector<std::string> rows;          // row-major, len = rows*cols
        int  column_count = 0;
        int  row_count    = 0;
        // First cell of the first row. Lets callers detect a plan
        // rowset by prefix-matching `<ShowPlanXML` without parsing.
        std::string first_cell_preview;
    };
    // Streams every result-set the script produced, with column names
    // via nanodbc::result::column_name. cancel() mid-flight surfaces
    // as LiveConnectError.
    std::vector<ResultSet> exec_with_resultsets(const std::string& sql);

    // Cancel any statement currently executing on this connection.
    // Thread-safe.
    void cancel();

    // @@VERSION (cached on connect).
    const std::string& server_version() const { return server_version_; }
    // SERVERPROPERTY('ProductMajorVersion'). 13=2016, 14=2017,
    // 15=2019, 16=2022. 0 when probe failed. Used to gate
    // version-specific DMVs.
    int server_major() const { return server_major_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool connected_ = false;
    std::string server_version_;
    int server_major_ = 0;
};

// Quote the value of an ODBC connection-string keyword by wrapping in
// {} and doubling internal '}' characters, per the MS DSN-less rules.
std::string odbc_quote(const std::string& v);

}  // namespace liveconnect
