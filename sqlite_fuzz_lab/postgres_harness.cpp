#include <iostream>
#include <postgresql/libpq-fe.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

__AFL_FUZZ_INIT();

int main(int argc, char **argv) {
    #ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
    #endif

    // 1. THIẾT LẬP KẾT NỐI MẠNG BÊN NGOÀI
    // Lưu ý: Đổi tên user/db/port theo môi trường Postgres thực tế của bạn
    const char *conninfo = "dbname=fuzz_db user=postgres password=postgres host=127.0.0.1 port=5433";
    PGconn *conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s", PQerrorMessage(conn));
        PQfinish(conn);
        exit(1);
    }

    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    // 2. VÒNG LẶP FUZZER TỐC ĐỘ CAO
    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        if (len == 0) continue;

        char *query = (char *)malloc(len + 1);
        if (!query) continue;
        memcpy(query, buf, len);
        query[len] = '\0';

        // 3. Thực thi truy vấn
        PGresult *res = PQexec(conn, query);
        
        // Bắt buộc phải clear để tránh tràn RAM
        if (res) {
            PQclear(res);
        }
        free(query);
    }

    PQfinish(conn);
    return 0;
}
