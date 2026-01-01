#include "ai_engine.h"
#include "../src/uci.h"
#include "../src/bitboard.h"
#include "../src/position.h"
#include "../src/tune.h"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <string>
#include <atomic>
#include <glib.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

using namespace Stockfish;

struct EngineHandle {
    bool is_internal;
    std::atomic<bool> running{false};
    
    // Message queues (used by both internal and external)
    std::queue<std::string> input_queue;
    std::mutex input_mutex;
    std::condition_variable input_cv;
    
    std::queue<std::string> output_queue;
    std::mutex output_mutex;
    std::condition_variable output_cv;
    
    // Internal specific
    std::thread internal_thread;
    
    // External specific
    GPid pid;
    gint stdin_pipe;
    gint stdout_pipe;
    GIOChannel* out_channel;
    std::thread reader_thread;
};

// Internal streambufs
class EngineInputBuf : public std::streambuf {
public:
    EngineInputBuf(EngineHandle* h) : handle(h) {}
protected:
    int underflow() override {
        std::unique_lock<std::mutex> lock(handle->input_mutex);
        handle->input_cv.wait(lock, [this] { return !handle->input_queue.empty() || !handle->running; });

        if (!handle->running && handle->input_queue.empty()) {
            return traits_type::eof();
        }

        current_line = handle->input_queue.front() + "\n";
        handle->input_queue.pop();
        
        setg(&current_line[0], &current_line[0], &current_line[0] + current_line.size());
        return traits_type::to_int_type(*gptr());
    }
private:
    EngineHandle* handle;
    std::string current_line;
};

class EngineOutputBuf : public std::streambuf {
public:
    EngineOutputBuf(EngineHandle* h) : handle(h) {}
protected:
    int overflow(int c) override {
        if (c == traits_type::eof()) return traits_type::not_eof(c);

        if (c == '\n') {
            std::lock_guard<std::mutex> lock(handle->output_mutex);
            handle->output_queue.push(current_line);
            current_line.clear();
            handle->output_cv.notify_all();
        } else {
            current_line += (char)c;
        }
        return c;
    }
private:
    EngineHandle* handle;
    std::string current_line;
};

static void internal_engine_main(EngineHandle* handle) {
    // Initialize Stockfish internals (once per process, but safe to call multiple times if we guarded)
    static std::once_flag sf_init_flag;
    std::call_once(sf_init_flag, [](){
        Bitboards::init();
        Position::init();
    });

    EngineInputBuf input_buf(handle);
    EngineOutputBuf output_buf(handle);
    
    // Redirect cin and cout (thread-local redirection would be better, but SF uses global cin/cout)
    // Note: This bridge assumes only one internal engine is active at a time, or that SF doesn't mind sharing.
    // Actually, SF UCI loop is quite self-contained if we redirect these.
    std::streambuf* old_cin = std::cin.rdbuf(&input_buf);
    std::streambuf* old_cout = std::cout.rdbuf(&output_buf);

    {
        char* argv[] = {(char*)"stockfish", nullptr};
        UCIEngine uci(1, argv);
        Tune::init(uci.engine_options());
        uci.loop();
    }

    std::cin.rdbuf(old_cin);
    std::cout.rdbuf(old_cout);
}

// External reader thread
static void external_reader_thread(EngineHandle* handle) {
    char buffer[4096];
    std::string current_line;
    
    while (handle->running) {
        gsize bytes_read = 0;
        GError* error = NULL;
        GIOStatus status = g_io_channel_read_chars(handle->out_channel, buffer, sizeof(buffer), &bytes_read, &error);
        
        if (status == G_IO_STATUS_NORMAL && bytes_read > 0) {
            for (gsize i = 0; i < bytes_read; i++) {
                if (buffer[i] == '\n') {
                    std::lock_guard<std::mutex> lock(handle->output_mutex);
                    handle->output_queue.push(current_line);
                    current_line.clear();
                    handle->output_cv.notify_all();
                } else if (buffer[i] != '\r') {
                    current_line += buffer[i];
                }
            }
        } else if (status == G_IO_STATUS_EOF || status == G_IO_STATUS_ERROR) {
            if (error) g_error_free(error);
            break;
        }
    }
}


extern "C" {

EngineHandle* ai_engine_init_internal(void) {
    EngineHandle* h = new EngineHandle();
    h->is_internal = true;
    h->running = true;
    h->internal_thread = std::thread(internal_engine_main, h);
    return h;
}

EngineHandle* ai_engine_init_external(const char* binary_path) {
    if (!binary_path) return nullptr;

    EngineHandle* h = new EngineHandle();
    h->is_internal = false;
    
    gchar* argv[] = {(gchar*)binary_path, nullptr};
    GError* error = NULL;
    
    if (!g_spawn_async_with_pipes(NULL, argv, NULL, (GSpawnFlags)(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH), 
                                 NULL, NULL, &h->pid, &h->stdin_pipe, &h->stdout_pipe, NULL, &error)) {
        if (error) {
            fprintf(stderr, "Failed to spawn engine: %s\n", error->message);
            g_error_free(error);
        }
        delete h;
        return nullptr;
    }

    h->running = true;
    h->out_channel = g_io_channel_unix_new(h->stdout_pipe);
    g_io_channel_set_encoding(h->out_channel, NULL, NULL);
    g_io_channel_set_buffered(h->out_channel, FALSE);

    h->reader_thread = std::thread(external_reader_thread, h);
    // h->writer_thread = std::thread(external_writer_thread, h); // We can write directly in send_command for simple pipes
    
    return h;
}

void ai_engine_cleanup(EngineHandle* handle) {
    if (!handle) return;

    ai_engine_send_command(handle, "quit");
    handle->running = false;
    handle->input_cv.notify_all();

    if (handle->is_internal) {
        if (handle->internal_thread.joinable()) {
            handle->internal_thread.join();
        }
    } else {
        g_spawn_close_pid(handle->pid);
        if (handle->reader_thread.joinable()) {
            handle->reader_thread.join();
        }
        g_io_channel_unref(handle->out_channel);
        #ifdef _WIN32
        _close(handle->stdin_pipe);
        _close(handle->stdout_pipe);
        #else
        close(handle->stdin_pipe);
        close(handle->stdout_pipe);
        #endif
    }

    delete handle;
}

void ai_engine_send_command(EngineHandle* handle, const char* command) {
    if (!handle || !command) return;

    if (handle->is_internal) {
        std::lock_guard<std::mutex> lock(handle->input_mutex);
        handle->input_queue.push(std::string(command));
        handle->input_cv.notify_one();
    } else {
        std::string cmd = std::string(command) + "\n";
        #ifdef _WIN32
        _write(handle->stdin_pipe, cmd.c_str(), cmd.size());
        #else
        write(handle->stdin_pipe, cmd.c_str(), cmd.size());
        #endif
    }
}

char* ai_engine_try_get_response(EngineHandle* handle) {
    if (!handle) return nullptr;
    
    std::lock_guard<std::mutex> lock(handle->output_mutex);
    if (handle->output_queue.empty()) return nullptr;

    std::string res = handle->output_queue.front();
    handle->output_queue.pop();
    
    #ifdef _WIN32
    return _strdup(res.c_str());
    #else
    return strdup(res.c_str());
    #endif
}

void ai_engine_free_response(char* response) {
    if (response) free(response);
}

char* ai_engine_wait_for_bestmove(EngineHandle* handle) {
    if (!handle) return nullptr;
    
    while (handle->running) {
        char* line = ai_engine_try_get_response(handle);
        if (line) {
            if (strncmp(line, "bestmove", 8) == 0) {
                return line;
            }
            ai_engine_free_response(line);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return nullptr;
}

void ai_engine_set_option(EngineHandle* handle, const char* name, const char* value) {
    if (!handle || !name || !value) return;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "setoption name %s value %s", name, value);
    ai_engine_send_command(handle, cmd);
}

bool ai_engine_test_binary(const char* binary_path) {
    if (!binary_path) return false;

    EngineHandle* h = ai_engine_init_external(binary_path);
    if (!h) return false;

    ai_engine_send_command(h, "uci");
    
    // Wait for uciok with timeout
    bool success = false;
    for (int i = 0; i < 200; i++) { // 2 seconds timeout
        char* res = ai_engine_try_get_response(h);
        if (res) {
            if (strncmp(res, "uciok", 5) == 0) {
                success = true;
                ai_engine_free_response(res);
                break;
            }
            ai_engine_free_response(res);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ai_engine_cleanup(h);
    return success;
}

AiDifficultyParams ai_get_difficulty_params(int elo) {
    AiDifficultyParams params;
    
    if (elo < 2000) {
        params.skill_level = (elo - 100) / 95; 
        if (params.skill_level > 20) params.skill_level = 20;
        if (params.skill_level < 0) params.skill_level = 0;
        params.depth = (elo < 1000) ? 1 : (elo < 1500) ? 3 : 5;
        params.move_time_ms = 50;
    } else if (elo < 2800) {
        params.skill_level = 20;
        params.depth = 5 + (elo - 2000) / 160; 
        params.move_time_ms = 100 + (elo - 2000) / 2;
    } else {
        params.skill_level = 20;
        params.depth = 10 + (elo - 2800) / 80;
        params.move_time_ms = 500 + (int)((elo - 2800) * 1.5);
    }
    
    return params;
}

} // extern "C"
