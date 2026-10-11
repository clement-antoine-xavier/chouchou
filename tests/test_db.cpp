#include <gtest/gtest.h>

#include <exception>
#include <string>

#include <pqxx/pqxx>

// Integration test: connects to a real Postgres instance. It self-skips when
// the database is not reachable so that `ctest` stays green without Docker.
TEST(Db, ServerVersion) {
    try {
        pqxx::connection conn;
        pqxx::work tx{conn};
        const std::string version = tx.exec("SELECT version()")[0][0].as<std::string>();
        tx.commit();

        EXPECT_NE(version.find("PostgreSQL"), std::string::npos) << "got: " << version;
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Postgres not available (" << e.what() << ")";
    }
}
