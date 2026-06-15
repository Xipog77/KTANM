#define _GNU_SOURCE
#include <sched.h>
#include <iostream>
#include <fstream>
#include <mysql/mysql.h>
#include <mysql/errmsg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>

extern "C" unsigned int __afl_map_size;

__AFL_FUZZ_INIT();

#include <string>

pid_t parent_pid = -1;
pid_t global_mysqld_pid = -1;

std::string fuzz_id = "01";
std::string pid_file_path = "/tmp/mysql_fuzz_01.pid";
std::string sock_file_path = "/tmp/mysql_fuzz_01.sock";
std::string sock_lock_path = "/tmp/mysql_fuzz_01.sock.lock";
std::string log_file_path = "mysqld_fuzz_01.log";
std::string harness_log_path = "harness_01.log";
std::string datadir_path = "/home/dokhanh/mysql_data_01";
int fuzz_mysql_port = 3308;

void init_paths() {
    const char *env_id = getenv("FUZZ_ID");
    if (env_id && *env_id) {
        fuzz_id = env_id;
    }
    pid_file_path = "/tmp/mysql_fuzz_" + fuzz_id + ".pid";
    sock_file_path = "/tmp/mysql_fuzz_" + fuzz_id + ".sock";
    sock_lock_path = "/tmp/mysql_fuzz_" + fuzz_id + ".sock.lock";
    log_file_path = "mysqld_fuzz_" + fuzz_id + ".log";
    harness_log_path = "harness_" + fuzz_id + ".log";
    datadir_path = "/home/dokhanh/mysql_data_" + fuzz_id;
    
    int id_num = 1;
    try {
        id_num = std::stoi(fuzz_id);
    } catch (...) {
        id_num = 1;
    }
    fuzz_mysql_port = 3300 + id_num;
}

bool is_process_alive(pid_t pid) {
    if (pid <= 0) return false;
    std::string path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream stat_file(path.c_str());
    if (!stat_file.is_open()) {
        return false;
    }
    std::string line;
    if (std::getline(stat_file, line)) {
        size_t last_paren = line.rfind(')');
        if (last_paren != std::string::npos && last_paren + 2 < line.size()) {
            char state = line[last_paren + 2];
            if (state == 'Z' || state == 'x' || state == 'X') {
                return false; // Zombie or Dead
            }
            return true;
        }
    }
    return false;
}

pid_t get_mysqld_pid_from_file() {
    std::ifstream pid_file(pid_file_path.c_str());
    if (pid_file.is_open()) {
        pid_t pid;
        if (pid_file >> pid) {
            return pid;
        }
    }
    return -1;
}

bool is_mysqld_alive() {
    pid_t pid = get_mysqld_pid_from_file();
    if (pid <= 0) return false;
    return is_process_alive(pid);
}

void cleanup_mysqld() {
    if (getpid() == parent_pid) {
        pid_t pid_to_kill = get_mysqld_pid_from_file();
        if (pid_to_kill <= 0) {
            pid_to_kill = global_mysqld_pid;
        }
        if (pid_to_kill > 0) {
            std::cout << "[Harness] Parent process exiting, shutting down mysqld (PID " << pid_to_kill << ")..." << std::endl;
            kill(pid_to_kill, SIGTERM);
            int status;
            waitpid(pid_to_kill, &status, 0);
            unlink(pid_file_path.c_str());
            unlink(sock_file_path.c_str());
            unlink(sock_lock_path.c_str());
            std::cout << "[Harness] Cleanup complete." << std::endl;
        }
    }
}

void sig_handler(int sig) {
    exit(0);
}

void kill_old_mysqld() {
    std::ifstream pid_file(pid_file_path.c_str());
    if (pid_file.is_open()) {
        pid_t old_pid;
        if (pid_file >> old_pid) {
            std::cout << "[Harness] Found old mysqld running with PID " << old_pid << ". Terminating it..." << std::endl;
            kill(old_pid, SIGTERM);
            // Wait up to 10 seconds for it to exit
            for (int i = 0; i < 10; i++) {
                if (kill(old_pid, 0) < 0) {
                    // Process is gone
                    break;
                }
                sleep(1);
            }
            // If still running, force kill
            if (kill(old_pid, 0) == 0) {
                std::cout << "[Harness] Old mysqld did not terminate. Sending SIGKILL..." << std::endl;
                kill(old_pid, SIGKILL);
                // Poll until the process is gone (waitpid won't work on orphaned processes)
                for (int j = 0; j < 50; j++) {
                    if (kill(old_pid, 0) < 0) {
                        break;
                    }
                    usleep(100000); // 100ms
                }
            }
        }
        pid_file.close();
    }
    unlink(pid_file_path.c_str());
    unlink(sock_file_path.c_str());
    unlink(sock_lock_path.c_str());
}

void start_mysqld() {
    // Clean up any stale instances before launching
    kill_old_mysqld();

    std::cout << "[Harness] Starting instrumented mysqld daemon..." << std::endl;
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        exit(1);
    }

    if (pid == 0) {
        // Unset the persistent fuzzing testcase shared memory IDs from the child's environment
        // so that the child mysqld doesn't try to map the read-only testcase buffer and segfault.
        unsetenv("__AFL_SHM_FUZZ_ID");
        unsetenv("__AFL_FUZZ_TESTCASE_ID");

        // Close all file descriptors from 3 to 1024 to prevent AFL++ control/status
        // descriptors from being inherited by mysqld. This prevents the child mysqld 
        // from hijacking the AFL++ pipes and getting stuck in its own forkserver loop.
        for (int fd_to_close = 3; fd_to_close < 1024; fd_to_close++) {
            close(fd_to_close);
        }

        // Child process: execute mysqld. Use O_APPEND so we don't lose logs across runs.
        int fd = open(log_file_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }

        // Exec mysql_install/bin/mysqld. Override options dynamically.
        char *args[9];
        args[0] = (char *)"mysql_install/bin/mysqld";
        args[1] = (char *)"--defaults-file=my_fuzz.cnf";
        
        std::string datadir_arg = "--datadir=" + datadir_path;
        std::string socket_arg = "--socket=" + sock_file_path;
        std::string pid_arg = "--pid-file=" + pid_file_path;
        std::string port_arg = "--port=" + std::to_string(fuzz_mysql_port);
        
        args[2] = strdup(datadir_arg.c_str());
        args[3] = strdup(socket_arg.c_str());
        args[4] = strdup(pid_arg.c_str());
        args[5] = strdup(port_arg.c_str());
        
        int next_arg = 6;
        if (geteuid() == 0) {
            args[next_arg++] = (char *)"--user=root";
        }
        args[next_arg] = NULL;
        
        // Reset CPU affinity mask so mysqld can run on any CPU core
        cpu_set_t mask;
        CPU_ZERO(&mask);
        for (int c = 0; c < sysconf(_SC_NPROCESSORS_ONLN); c++) {
            CPU_SET(c, &mask);
        }
        sched_setaffinity(0, sizeof(mask), &mask);
        
        execv(args[0], args);
        perror("execv mysqld failed");
        exit(1);
    }

    // Parent process
    global_mysqld_pid = pid;
    std::cout << "[Harness] Spawned mysqld with PID " << pid << ". Waiting for socket " << sock_file_path << " to be ready..." << std::endl;

    MYSQL *conn = NULL;
    for (int i = 0; i < 30; i++) {
        // Check if the child process has exited prematurely
        int status = 0;
        pid_t wait_res = waitpid(pid, &status, WNOHANG);
        if (wait_res > 0) {
            std::cerr << "[Harness] Child mysqld process (PID " << pid << ") exited prematurely!" << std::endl;
            if (WIFEXITED(status)) {
                std::cerr << "[Harness] Exit status: " << WEXITSTATUS(status) << std::endl;
            } else if (WIFSIGNALED(status)) {
                std::cerr << "[Harness] Killed by signal: " << WTERMSIG(status) << std::endl;
            }
            exit(1);
        } else if (wait_res < 0) {
            perror("[Harness] waitpid failed");
            exit(1);
        }

        conn = mysql_init(NULL);
        if (conn == NULL) {
            std::cerr << "mysql_init failed" << std::endl;
            exit(1);
        }
        if (mysql_real_connect(conn, NULL, "root", "", NULL, 0, sock_file_path.c_str(), 0)) {
            std::cout << "[Harness] Successfully connected to mysqld after " << i << " seconds." << std::endl;
            break;
        }
        mysql_close(conn);
        conn = NULL;
        sleep(1);
    }

    if (!conn) {
        std::cerr << "[Harness] Failed to connect to mysqld via Unix socket " << sock_file_path << ". Killing child." << std::endl;
        kill(pid, SIGKILL);
        int status;
        waitpid(pid, &status, 0);
        exit(1);
    }

    // Ensure database exists
    if (mysql_query(conn, "CREATE DATABASE IF NOT EXISTS fuzz_db") != 0) {
        std::cerr << "Warning: failed to create fuzz_db: " << mysql_error(conn) << std::endl;
    }
    mysql_close(conn);
}

int main(int argc, char **argv) {
    // Initialize paths from environment variables
    init_paths();

    // Override the AFL map size so that the forkserver reports 8MB (maximum supported) to afl-fuzz
    __afl_map_size = 8388608;

    // Redirect stdout and stderr of the harness to harness_<FUZZ_ID>.log
    int log_fd = open(harness_log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd >= 0) {
        dup2(log_fd, 1);
        dup2(log_fd, 2);
        close(log_fd);
    }

    std::cout << "\n--- New Run Started (PID: " << getpid() << ") ---" << std::endl;

    parent_pid = getpid();
    
    // Register exit handlers and signal catchers
    atexit(cleanup_mysqld);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // 1. Start mysqld server (only once in the main fuzzer parent before __AFL_INIT)
    start_mysqld();

    // 2. AFL forkserver handshake
    #ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
    #endif

    // 3. Connect to the running server (done by each child fork)
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        exit(1);
    }

    if (mysql_real_connect(conn, NULL, "root", "", "fuzz_db", 0, sock_file_path.c_str(), 0) == NULL) {
        fprintf(stderr, "mysql_real_connect() failed: %s. Checking if mysqld is alive...\n", mysql_error(conn));
        if (!is_mysqld_alive()) {
            std::cout << "[Harness] mysqld is dead. Restarting before entering fuzzing loop..." << std::endl;
            start_mysqld();
            if (mysql_real_connect(conn, NULL, "root", "", "fuzz_db", 0, sock_file_path.c_str(), 0) == NULL) {
                fprintf(stderr, "mysql_real_connect() failed after restart: %s\n", mysql_error(conn));
                mysql_close(conn);
                exit(1);
            }
        } else {
            mysql_close(conn);
            exit(1);
        }
    }

    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    // 4. AFL persistent loop
    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        if (len == 0) continue;

        // Clear the table to prevent table bloat and state contamination
        mysql_query(conn, "DELETE FROM fuzz_tb");

        char *query = (char *)malloc(len + 1);
        if (!query) continue;
        memcpy(query, buf, len);
        query[len] = '\0';

        // Execute query on the server
        if (mysql_query(conn, query) == 0) {
            MYSQL_RES *result = mysql_store_result(conn);
            if (result) {
                mysql_free_result(result);
            }
        } else {
            unsigned int err_no = mysql_errno(conn);
            if (err_no == CR_SERVER_GONE_ERROR || err_no == CR_SERVER_LOST || !is_mysqld_alive()) {
                fprintf(stderr, "[Harness] MySQL query failed with connection error (%u) or daemon is dead. Aborting to report crash to AFL++...\n", err_no);
                free(query);
                mysql_close(conn);
                abort();
            }
        }
        
        free(query);
    }

    mysql_close(conn);
    return 0;
}
