#include "liveconnect/liveconnect.hpp"

#include <nanodbc/nanodbc.h>

#include <atomic>
#include <cstdlib>
#include <sstream>

namespace liveconnect {

namespace {

std::string build_conn_string(const LiveConnectParams& p) {
    std::ostringstream s;
    // Driver 18 is MS's current driver on Linux and Windows. Users
    // on older drivers will need to adjust.
    s << "Driver={ODBC Driver 18 for SQL Server};";
    s << "Server=" << p.server << ";";
    if (!p.database.empty()) s << "Database=" << p.database << ";";
    if (p.auth == LiveAuth::Integrated) {
        s << "Trusted_Connection=yes;";
    } else {
        s << "UID=" << p.user << ";";
        s << "PWD={" << p.password << "};";
    }
    s << "Encrypt=" << (p.encrypt ? "yes" : "no") << ";";
    s << "TrustServerCertificate="
      << (p.trust_server_certificate ? "yes" : "no") << ";";
    s << "Application Name=calliper;";
    return s.str();
}

// Walks a result column producing the cell value as a std::string.
// nanodbc::result::get<std::string> already loops SQLGetData for long
// data internally, so this is just a NULL-aware wrapper.
std::string get_cell(nanodbc::result& r, short col) {
    if (r.is_null(col)) return {};
    try {
        return r.get<std::string>(col, "");
    } catch (const nanodbc::database_error&) {
        return {};
    }
}

}  // namespace

std::string odbc_quote(const std::string& v) {
    // Curly-brace quoting handles every byte except '}' inside the
    // value, which must be doubled. Used for any keyword whose value
    // might contain a semicolon (passwords, paths, certs).
    std::string out = "{";
    for (char c : v) {
        if (c == '}') out += "}}";
        else out += c;
    }
    out += "}";
    return out;
}

struct LiveConnection::Impl {
    nanodbc::connection conn;
    // Statement currently executing on this connection. Read by
    // cancel() from another thread under stmt_mu_; the executing
    // function clears it before destroying the statement so cancel()
    // never dereferences a dangling pointer.
    nanodbc::statement* active_stmt = nullptr;
    std::mutex stmt_mu;
};

LiveConnection::LiveConnection() : impl_(std::make_unique<Impl>()) {}
LiveConnection::~LiveConnection() { disconnect(); }

LiveConnection::LiveConnection(LiveConnection&& o) noexcept
    : impl_(std::move(o.impl_)),
      connected_(o.connected_),
      server_version_(std::move(o.server_version_)),
      server_major_(o.server_major_) {
    o.connected_ = false;
    o.server_major_ = 0;
}

LiveConnection& LiveConnection::operator=(LiveConnection&& o) noexcept {
    if (this == &o) return *this;
    disconnect();
    impl_ = std::move(o.impl_);
    connected_ = o.connected_; o.connected_ = false;
    server_version_ = std::move(o.server_version_);
    server_major_ = o.server_major_; o.server_major_ = 0;
    return *this;
}

void LiveConnection::connect(const LiveConnectParams& p) {
    if (connected_) disconnect();
    try {
        impl_->conn.connect(build_conn_string(p),
                            static_cast<long>(p.login_timeout_s));
    } catch (const nanodbc::database_error& e) {
        throw LiveConnectError(std::string("Connect failed: ") + e.what());
    }
    connected_ = true;

    // Cache @@VERSION so the dialog can show what we connected to.
    try {
        server_version_ = exec_scalar_text("SELECT @@VERSION");
    } catch (...) {
        server_version_.clear();
    }
    // ProductMajorVersion (13=2016, 14=2017, 15=2019, 16=2022).
    // 0 means probe failed; caller treats it as "no support".
    try {
        std::string s = exec_scalar_text(
            "SELECT CAST(SERVERPROPERTY('ProductMajorVersion') AS NVARCHAR(16))");
        server_major_ = s.empty() ? 0 : std::atoi(s.c_str());
    } catch (...) {
        server_major_ = 0;
    }
}

void LiveConnection::disconnect() {
    if (!impl_) return;
    if (connected_) {
        try { impl_->conn.disconnect(); } catch (...) {}
        connected_ = false;
    }
}

bool LiveConnection::is_connected() const { return connected_; }

namespace {

// RAII guard: register a statement as the cancellation target on
// entry, clear it on exit. The mutex protects against a cancel()
// thread observing the pointer mid-destruction. Takes the slot by
// reference to avoid depending on the private Impl type.
class CancelScope {
public:
    CancelScope(std::mutex& m, nanodbc::statement** slot,
                nanodbc::statement* s)
        : mu_(m), slot_(slot) {
        std::lock_guard<std::mutex> g(mu_);
        *slot_ = s;
    }
    ~CancelScope() {
        std::lock_guard<std::mutex> g(mu_);
        *slot_ = nullptr;
    }
private:
    std::mutex& mu_;
    nanodbc::statement** slot_;
};

}  // namespace

void LiveConnection::exec(const std::string& sql) {
    if (!connected_) throw LiveConnectError("exec: not connected");
    try {
        nanodbc::just_execute(impl_->conn, sql);
    } catch (const nanodbc::database_error& e) {
        throw LiveConnectError(std::string("exec: ") + e.what());
    }
}

std::string LiveConnection::exec_scalar_text(const std::string& sql) {
    if (!connected_)
        throw LiveConnectError("exec_scalar_text: not connected");
    try {
        nanodbc::statement st(impl_->conn);
        CancelScope cs(impl_->stmt_mu, &impl_->active_stmt, &st);
        nanodbc::result r = st.execute_direct(impl_->conn, sql);
        if (!r.next()) return {};
        return get_cell(r, 0);
    } catch (const nanodbc::database_error& e) {
        throw LiveConnectError(std::string("exec_scalar_text: ") + e.what());
    }
}

std::vector<std::string> LiveConnection::exec_text_rows(
        const std::string& sql) {
    if (!connected_)
        throw LiveConnectError("exec_text_rows: not connected");
    std::vector<std::string> out;
    try {
        nanodbc::statement st(impl_->conn);
        CancelScope cs(impl_->stmt_mu, &impl_->active_stmt, &st);
        nanodbc::result r = st.execute_direct(impl_->conn, sql);
        do {
            short n_cols = r.columns();
            if (n_cols <= 0) continue;
            while (r.next()) {
                for (short c = 0; c < n_cols; ++c) {
                    out.push_back(get_cell(r, c));
                }
            }
        } while (r.next_result());
    } catch (const nanodbc::database_error& e) {
        throw LiveConnectError(std::string("exec_text_rows: ") + e.what());
    }
    return out;
}

std::vector<LiveConnection::ResultSet>
LiveConnection::exec_with_resultsets(const std::string& sql) {
    if (!connected_)
        throw LiveConnectError("exec_with_resultsets: not connected");
    std::vector<ResultSet> out;
    try {
        nanodbc::statement st(impl_->conn);
        CancelScope cs(impl_->stmt_mu, &impl_->active_stmt, &st);
        nanodbc::result r = st.execute_direct(impl_->conn, sql);
        do {
            short n_cols = r.columns();
            if (n_cols <= 0) continue;

            ResultSet rs;
            rs.column_count = n_cols;
            rs.column_names.reserve(n_cols);
            for (short c = 0; c < n_cols; ++c) {
                rs.column_names.emplace_back(r.column_name(c));
            }
            while (r.next()) {
                for (short c = 0; c < n_cols; ++c) {
                    rs.rows.push_back(get_cell(r, c));
                }
                if (rs.first_cell_preview.empty() && !rs.rows.empty()) {
                    rs.first_cell_preview = rs.rows.front().substr(
                        0, std::min<size_t>(64, rs.rows.front().size()));
                }
                ++rs.row_count;
            }
            out.push_back(std::move(rs));
        } while (r.next_result());
    } catch (const nanodbc::database_error& e) {
        throw LiveConnectError(std::string("exec_with_resultsets: ") +
                               e.what());
    }
    return out;
}

void LiveConnection::cancel() {
    if (!impl_) return;
    std::lock_guard<std::mutex> g(impl_->stmt_mu);
    if (impl_->active_stmt) {
        try { impl_->active_stmt->cancel(); } catch (...) {}
    }
}

}  // namespace liveconnect
