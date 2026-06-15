# Bài tập lớn cuối kỳ - Môn học: Kiểm thử an ninh mạng

**Thông tin sinh viên:**
* **Họ và tên:** Đỗ Ngọc Khánh
* **Mã sinh viên:** 23020615
* **Môn học:** Kiểm thử an ninh mạng (2526II_INT3327_1)
* **Giảng viên:** Lê Đình Thanh

---

# Hướng Dẫn Chạy Chiến Dịch Fuzzing (SQLite 3, MySQL, PostgreSQL)

Tài liệu này hướng dẫn nhanh cách biên dịch và chạy chiến dịch Fuzzing trên SQLite 3, MySQL 8.0.20, và PostgreSQL 12.2 bằng **AFL++** kết hợp **Nautilus Custom SQL Mutator**.

## 1. Chuẩn Bị Môi Trường

> [!IMPORTANT]
> Các lệnh và kịch bản dưới đây sử dụng đường dẫn tuyệt đối `/home/dokhanh/Desktop/data/Lab/KTANM/`. Nếu bạn chạy trên một máy tính hoặc thư mục khác, hãy thay đổi đường dẫn này thành đường dẫn tương ứng trên hệ thống của bạn.

### A. Cài đặt các gói phụ thuộc hệ thống
```bash
sudo apt update
sudo apt install -y build-essential python3-dev pip clang lld llvm llvm-dev libclang-dev ninja-build valgrind uuid-dev default-jre python3
```

### B. Biên dịch và Cài đặt AFL++
```bash
cd AFLplusplus-5.00c
make clean
make distrib
sudo make install
```

### C. Biên dịch Custom SQL Mutator (Grammar-Mutator)
```bash
cd Grammar-Mutator
make clean
make GRAMMAR_FILE=sql_grammar.json
```
Sau khi biên dịch thành công, liên kết động `libgrammarmutator-sql.so` sẽ được tạo ra tại thư mục `Grammar-Mutator/`.

### D. Cấu hình Tài Nguyên
* **Fuzzer Core**: AFL++ (Sử dụng trình biên dịch `afl-clang-fast` / `afl-clang-fast++`)
* **Mutator**: `/home/dokhanh/Desktop/data/Lab/KTANM/Grammar-Mutator/libgrammarmutator-sql.so`
* **Dữ liệu**: Hạt giống tại `inputs/`, kết quả lưu tại `outputs/`

---

## 2. Các Bước Khởi Chạy Fuzzing

### A. SQLite 3 (In-Memory Fuzzing)
1. **Biên dịch**:
   ```bash
   afl-clang-fast -O3 sqlite_harness.c sqlite_src/sqlite3.c -o sqlite_fuzzer -lpthread -ldl -lm
   ```
2. **Khởi chạy**:
   ```bash
   env AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
   AFL_CUSTOM_MUTATOR_LIBRARY=/home/dokhanh/Desktop/data/Lab/KTANM/Grammar-Mutator/libgrammarmutator-sql.so \
   AFL_CUSTOM_MUTATOR_ONLY=1 \
   AFL_CUSTOM_INFO_PROGRAM=./sqlite_fuzzer \
   AFL_CUSTOM_INFO_PROGRAM_ARGV="" \
   AFL_CUSTOM_INFO_OUT=outputs/sqlite_fuzzer01 \
   afl-fuzz -i inputs/ -o outputs/ -M sqlite_fuzzer01 -- ./sqlite_fuzzer
   ```

### B. PostgreSQL 12.2
* **Cách 1: Single-User Mode (Mặc định - Khuyên dùng)**
  Fuzz trực tiếp qua stdin của `postgres --single` không cần kết nối socket:
  ```bash
  # Cú pháp: ./run_postgres_fuzz.sh <Số_cores> <Offset>
  ./run_postgres_fuzz.sh 2 0
  ```
* **Cách 2: Client-Server Harness (Thông qua `libpq`)**
  1. Biên dịch Harness:
     ```bash
     afl-clang-fast++ -O3 postgres_harness.cpp -o postgres_fuzzer -I$(pg_config --includedir) -L$(pg_config --libdir) -lpq -lstdc++
     ```
  2. Khởi chạy:
     ```bash
     env AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
     AFL_CUSTOM_MUTATOR_LIBRARY=/home/dokhanh/Desktop/data/Lab/KTANM/Grammar-Mutator/libgrammarmutator-sql.so \
     AFL_CUSTOM_MUTATOR_ONLY=1 \
     AFL_CUSTOM_INFO_PROGRAM=./postgres_fuzzer \
     AFL_CUSTOM_INFO_PROGRAM_ARGV="" \
     AFL_CUSTOM_INFO_OUT=outputs/postgres_fuzzer01 \
     afl-fuzz -i inputs/ -o outputs/ -M postgres_fuzzer01 -- ./postgres_fuzzer
     ```

### C. MySQL 8.0.20 (Client-Socket Connection)
Khởi chạy tự động (quản lý daemon `mysqld` và cổng socket riêng biệt cho từng core):
```bash
# Cú pháp: ./run_mysql_fuzz.sh <Số_cores> <Offset>
./run_mysql_fuzz.sh 2 0
```

---

## 3. Các Lệnh Bảo Trì Thường Dùng

* **Dọn dẹp tiến trình treo nền**:
  ```bash
  pkill -f mysql_fuzzer
  pkill -f mysql_install/bin/mysqld
  pkill -f postgres_fuzzer
  ```
* **Sửa lỗi phân quyền thư mục kết quả (Permission Denied)**:
  ```bash
  sudo chown -R $USER:$USER /home/dokhanh/Desktop/data/Lab/KTANM/sqlite_fuzz_lab/outputs/
  ```
* **Vẽ đồ thị giám sát hiệu năng**:
  ```bash
  afl-plot outputs/sqlite_fuzzer01 outputs/sqlite_fuzzer01/plot
  ```
