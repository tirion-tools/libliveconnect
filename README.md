# libliveconnect

Thin C++17 SQL Server ODBC wrapper. Connection, scalar query, result-set capture, mid-flight cancel.

Maintained by [Tirion](https://tirion.tools). Used by [Calliper](https://github.com/tirion-tools/calliper). MIT.

## Status

1.0. Stable API.

## Surface

Everything in `include/liveconnect/liveconnect.hpp`:

| Type | Purpose |
|---|---|
| `liveconnect::LiveConnectError` | Exception with joined SQLSTATE + diagnostic chain |
| `liveconnect::LiveAuth` | `SqlLogin` or `Integrated` |
| `liveconnect::LiveConnectParams` | server / db / auth / encrypt / trust-cert / login-timeout |
| `liveconnect::LiveConnection` | RAII handle. `connect`, `disconnect`, `is_connected`, `cancel` |
| `LiveConnection::exec(sql)` | No-result statement (DDL, SET) |
| `LiveConnection::exec_scalar_text(sql)` | First text cell of first row |
| `LiveConnection::exec_text_rows(sql)` | All text cells, every result set, flat |
| `LiveConnection::exec_with_resultsets(sql)` | Typed `ResultSet` per emitted set |
| `LiveConnection::server_version()` / `server_major()` | Cached `@@VERSION` and `ProductMajorVersion` |
| `liveconnect::odbc_quote(value)` | Brace-quote a connection-string keyword value |

`cancel()` is thread-safe per ODBC's documented `SQLCancel` contract. Use it to interrupt a long-running query from a UI.

## Usage

```cpp
#include <liveconnect/liveconnect.hpp>

liveconnect::LiveConnectParams p;
p.server   = "prodsql2-22";
p.database = "MyDb";
p.user     = "service-user";
p.password = secret;
p.trust_server_certificate = true;

liveconnect::LiveConnection c;
c.connect(p);
auto ver = c.exec_scalar_text("SELECT @@VERSION");
printf("connected to %s\n", ver.c_str());

auto rs = c.exec_with_resultsets("SELECT id, name FROM dbo.Foo");
for (const auto& set : rs) {
    for (int r = 0; r < set.row_count; ++r) {
        for (int col = 0; col < set.column_count; ++col) {
            printf("  %s=%s",
                   set.column_names[col].c_str(),
                   set.rows[r * set.column_count + col].c_str());
        }
        printf("\n");
    }
}
```

## Building

```bash
sudo apt-get install -y build-essential cmake unixodbc-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

CMake integration:

```cmake
add_subdirectory(path/to/libliveconnect)
target_link_libraries(my_app PRIVATE liveconnect::liveconnect)
```

## License

MIT. Issues at [github.com/tirion-tools/libliveconnect](https://github.com/tirion-tools/libliveconnect).
