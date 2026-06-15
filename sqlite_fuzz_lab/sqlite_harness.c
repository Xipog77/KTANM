#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite_src/sqlite3.h"

// Khai báo macro cho AFL++ Persistent Mode
__AFL_FUZZ_INIT();

int main(int argc, char **argv) {
    #ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
    #endif

    sqlite3 *db;
    char *err_msg = 0;
    
    // Khởi tạo biến nhận dữ liệu từ Fuzzer
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    // Vòng lặp Fuzzer: Chạy 10,000 lần trước khi khởi động lại tiến trình
    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        if (len == 0) continue;

        // Cấp phát bộ nhớ và tạo chuỗi string kết thúc bằng null (\0)
        char *query = (char *)malloc(len + 1);
        if (!query) continue;
        memcpy(query, buf, len);
        query[len] = '\0';

        // Mở Database trên RAM (Cực kỳ quan trọng để tối ưu tốc độ)
        if (sqlite3_open(":memory:", &db) == SQLITE_OK) {
            
            // Tùy chọn: Bạn có thể tạo sẵn bảng trước khi Fuzz
            // sqlite3_exec(db, "CREATE TABLE users(id INT, name TEXT);", 0, 0, 0);

            // Bắn câu lệnh SQL bị đột biến vào hệ thống
            sqlite3_exec(db, query, 0, 0, &err_msg);
            
            // Clean Teardown: Dọn dẹp sạch sẽ để tránh tràn RAM
            if (err_msg) { 
                sqlite3_free(err_msg); 
                err_msg = 0;
            }
            sqlite3_close(db);
        }
        
        free(query);
    }
    return 0;
}
