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
> Nếu bạn chạy trên một máy tính hoặc thư mục khác, hãy thay đổi đường dẫn tuyệt đối thành đường dẫn tương ứng trên hệ thống của bạn.

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
* **Mutator**: `Grammar-Mutator/libgrammarmutator-sql.so`
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
   AFL_CUSTOM_MUTATOR_LIBRARY=/Grammar-Mutator/libgrammarmutator-sql.so \
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
     AFL_CUSTOM_MUTATOR_LIBRARY=/Grammar-Mutator/libgrammarmutator-sql.so \
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

## 3. Hướng Dẫn Biên Dịch DBMS từ Nguồn

### A. Biên dịch OpenSSL (Phụ thuộc tĩnh của MySQL)
MySQL yêu cầu thư viện SSL. Ta tiến hành biên dịch tĩnh OpenSSL bằng `afl-clang-fast`:
1. Di chuyển vào thư mục nguồn OpenSSL:
   ```bash
   cd sqlite_fuzz_lab/openssl-1.1.1w
   ```
2. Cấu hình và cài đặt:
   ```bash
   CC=afl-clang-fast ./config --prefix=$(pwd)/../openssl_install no-shared no-tests
   make -j$(nproc)
   make install
   ```

### B. Biên dịch MySQL 8.0.20
MySQL yêu cầu thư viện Boost và OpenSSL tĩnh vừa cài đặt ở trên:
1. Tạo thư mục build và di chuyển vào:
   ```bash
   cd sqlite_fuzz_lab/mysql-8.0.20
   mkdir -p build && cd build
   ```
2. Cấu hình bằng `cmake` sử dụng các trình biên dịch của AFL++:
   ```bash
   CC=afl-clang-fast CXX=afl-clang-fast++ cmake .. \
     -DCMAKE_INSTALL_PREFIX=$(pwd)/../../mysql_install \
     -DWITH_BOOST=$(pwd)/../../boost \
     -DWITH_SSL=$(pwd)/../../openssl_install \
     -DFORCE_UNSUPPORTED_COMPILER=ON \
     -DDEFAULT_CHARSET=utf8 \
     -DDEFAULT_COLLATION=utf8_general_ci
   ```
3. Biên dịch và cài đặt:
   ```bash
   make -j$(nproc)
   make install
   ```

### C. Biên dịch PostgreSQL 12.2
PostgreSQL được cấu hình không sử dụng `readline` và `zlib` để tối ưu cho việc fuzzing qua Single-User mode:
1. Di chuyển vào thư mục nguồn PostgreSQL:
   ```bash
   cd sqlite_fuzz_lab/postgresql-12.2
   ```
2. Cấu hình với trình biên dịch AFL++:
   ```bash
   CC=afl-clang-fast CXX=afl-clang-fast++ ./configure \
     --prefix=$(pwd)/../pg_install \
     --without-readline \
     --without-zlib
   ```
3. Biên dịch và cài đặt:
   ```bash
   make -j$(nproc)
   make install
   ```

### D. Chọn phiên bản SQLite 3
Có thể chuyển đổi linh hoạt giữa SQLite 3.34 và SQLite 3.44 bằng cách thay đổi liên kết động `sqlite_src`:
* **Chọn SQLite 3.34 (Mặc định)**:
  ```bash
  cd sqlite_fuzz_lab
  ln -sf sqlite_3.34 sqlite_src
  ```
* **Chọn SQLite 3.44**:
  ```bash
  cd sqlite_fuzz_lab
  ln -sf sqlite_3.44 sqlite_src
  ```
Sau khi chuyển đổi, tiến hành biên dịch lại harness theo chỉ dẫn ở **Mục 2.A**.
