// speedread.cpp — C++17 port of Petr Baudis' "speedread" Perl RSVP reader (MIT).
// Features: ORP pivot, ANSI colors, raw TTY input, [ ] speed control, space pause.
// Usage: cat file.txt | ./speedread [-w WPM] [-r RESUME] [-m]
// Notes: UTF-8 shown as bytes for width; complex graphemes may misalign in some terminals.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <getopt.h>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

using namespace std::chrono;

// -------- Config (mirrors Perl defaults) --------
static int   g_wpm            = 250;
static int   g_resume         = 0;
static bool  g_multiword      = false;

static double wordtime   = 0.9;   // relative to wpm
static double lentime    = 0.04;  // * sqrt(len)
static double commatime  = 2.0;   // relative to wpm
static double fstoptime  = 3.0;   // relative to wpm
static double multitime  = 1.2;   // relative to wpm
static double firsttime  = 0.2;   // seconds (absolute)
static int    ORPvisualpos = 20;  // caret 'v' column
static int    cursorpos    = 64;  // rightmost status column

// ANSI helpers
static const char* C_RESET = "\033[0m";
static const char* C_BOLD  = "\033[1m";
static const char* C_RED   = "\033[31m";
static const char* C_YELLOW= "\033[33m";
static const char* CLR2EOL = "\033[K";
static const char* CR      = "\r";
static const char* UP1     = "\033[A";

// Globals for stats and state
static volatile sig_atomic_t g_sigint = 0;
static size_t wordcounter = 0;
static size_t lettercounter = 0;
static bool paused = false;
static std::array<std::string, 2> lastlines{};

// timing anchor
static steady_clock::time_point t0;

// TTY raw mode management
struct RawTTY {
    termios orig{};
    int fd = -1;
    bool ok = false;

    RawTTY() {
        fd = open("/dev/tty", O_RDWR | O_NONBLOCK);
        if (fd < 0) return;
        if (tcgetattr(fd, &orig) != 0) return;

        termios raw = orig;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;   // read blocks for at least 1 byte (we'll use select)
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(fd, TCSANOW, &raw) != 0) return;

        ok = true;
    }
    ~RawTTY() {
        if (fd >= 0) {
            tcsetattr(fd, TCSANOW, &orig);
            close(fd);
        }
    }

    bool key_pressed() {
        if (!ok) return false;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        timeval tv{0,0};
        int r = select(fd+1, &rfds, nullptr, nullptr, &tv);
        return (r > 0 && FD_ISSET(fd, &rfds));
    }
    int getch() {
        if (!ok) return -1;
        char c;
        ssize_t n = ::read(fd, &c, 1);
        return (n == 1) ? static_cast<unsigned char>(c) : -1;
    }
};

// ORP (pivot) index (0-based) by word length; 14+ => 4
static int find_ORP(const std::string& w) {
    size_t L = w.size();
    if (L > 13) return 4;
    static const int table[14] = {
        0,0,1,1,1,1,2,2,2,2,3,3,3,3
    };
    return table[L];
}

static void show_guide() {
    // top line: caret aligned to ORPvisualpos
    std::cout << std::string(ORPvisualpos, ' ') << C_RED << "v" << C_RESET << CLR2EOL << "\n";
    std::cout.flush();
}

static void show_word(const std::string& word, int iPivot, int wpm, bool pausedFlag) {
    char pivot = (0 <= iPivot && iPivot < (int)word.size()) ? word[(size_t)iPivot] : ' ';
    if (pivot == ' ') pivot = '\xC2'; // We'll display '·' as best effort below if simple space
    // Render line
    std::string left  = (iPivot > 0) ? word.substr(0, (size_t)iPivot) : "";
    std::string piv   = (iPivot >= 0 && iPivot < (int)word.size()) ? word.substr((size_t)iPivot, 1) : " ";
    if (piv == " ") piv = "·";
    std::string right = ((size_t)iPivot+1 < word.size()) ? word.substr((size_t)iPivot+1) : "";

    // compute spacing to keep pivot aligned under caret
    int leftpad = ORPvisualpos - iPivot;
    if (leftpad < 0) leftpad = 0;
    int usedCols = leftpad + (int)word.size();
    int rightpad = std::max(0, cursorpos - usedCols);

    std::cout << CR << CLR2EOL
              << std::string(leftpad, ' ')
              << C_BOLD << left
              << C_RED  << piv
              << C_RESET << C_BOLD << right << C_RESET
              << std::string(rightpad, ' ')
              << wpm << " wpm";
    if (pausedFlag) std::cout << "  " << C_YELLOW << "PAUSED" << C_RESET;
    std::cout.flush();
}

// word display time in seconds (same heuristics as Perl)
static double word_time(const std::string& w, int wpm, size_t wordIndexSoFar) {
    double t = wordtime;
    if (!w.empty()) {
        char last = w.back();
        if (last == '.' || last == '?' || last == '!') {
            t = fstoptime;
        } else if (last == ':' || last == ';' || last == ',') {
            t = commatime;
        } else if (w.find(' ') != std::string::npos) {
            t = multitime;
        }
    }
    t += std::sqrt((double)w.size()) * lentime;
    t *= 60.0 / std::max(1, wpm);

    if (wordIndexSoFar == 0 && t < firsttime) t = firsttime;
    return t;
}

static void print_context(int wn, const std::array<std::string,2>& ctx, const std::string& currentWord) {
    // Move to previous line, clear two lines, reprint context with current word highlighted in second line.
    std::cout << CR << CLR2EOL << UP1 << CLR2EOL;
    if (!ctx[1].empty()) std::cout << ctx[1] << "\n";
    std::string line0 = ctx[0];
    // naive highlight of the wn-th token (0-based) separated by (?:-|\s)+
    // We'll do a simple split & rebuild:
    // Find tokens:
    std::vector<std::pair<size_t,size_t>> spans; // [start,end)
    size_t i = 0, n = line0.size();
    auto is_sep = [](char c){ return std::isspace((unsigned char)c) || c=='-'; };
    while (i < n) {
        while (i < n && is_sep(line0[i])) i++;
        if (i >= n) break;
        size_t j = i;
        while (j < n && !is_sep(line0[j])) j++;
        spans.push_back({i,j});
        i = j;
    }
    if (wn > 0 && wn-1 < (int)spans.size()) {
        auto [s,e] = spans[(size_t)(wn-1)];
        std::cout << line0.substr(0,s)
                  << C_YELLOW << line0.substr(s, e-s) << C_RESET
                  << line0.substr(e) << "\n";
    } else {
        std::cout << line0 << "\n";
    }
    std::cout.flush();
}

static void print_stats_and_resume_hint() {
    auto elapsed = duration_cast<duration<double>>(steady_clock::now() - t0).count();
    double truewpm = (elapsed > 0.0) ? (wordcounter / elapsed * 60.0) : 0.0;
    std::cout << "\n " << std::fixed;
    std::cout.setf(std::ios::fixed); std::cout.precision(2);
    std::cout << elapsed << "s, " << wordcounter << " words, " << lettercounter
              << " letters, " << C_BOLD << "\033[32m" << truewpm << "\033[0m" << " true wpm\n";
    size_t resume_word = wordcounter + (size_t)g_resume;
    std::cout << " To resume from this point run with argument -r " << resume_word << "\n";
    std::cout.flush();
}

static void on_sigint(int) { g_sigint = 1; }

static std::vector<std::string> split_tokens(const std::string& line) {
    // split on (?:-|\s)+ and drop empties
    std::vector<std::string> out;
    size_t i=0, n=line.size();
    auto is_sep = [](char c){ return std::isspace((unsigned char)c) || c=='-'; };
    while (i<n) {
        while (i<n && is_sep(line[i])) i++;
        if (i>=n) break;
        size_t j=i;
        while (j<n && !is_sep(line[j])) j++;
        out.emplace_back(line.substr(i, j-i));
        i=j;
    }
    return out;
}

int main(int argc, char** argv) {
    // Parse flags
    static option longopts[] = {
        {"wpm",      required_argument, nullptr, 'w'},
        {"resume",   required_argument, nullptr, 'r'},
        {"multiword",no_argument,       nullptr, 'm'},
        {nullptr,0,nullptr,0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "w:r:m", longopts, nullptr)) != -1) {
        switch (opt) {
            case 'w': g_wpm = std::max(1, atoi(optarg)); break;
            case 'r': g_resume = std::max(0, atoi(optarg)); break;
            case 'm': g_multiword = true; break;
            default:
                std::fprintf(stderr, "Usage: %s [-w WPM] [-r RESUME] [-m]\n", argv[0]);
                return 1;
        }
    }

    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Setup raw TTY input
    RawTTY tty;

    // Handle Ctrl-C
    std::signal(SIGINT, on_sigint);

    // Top guide caret
    show_guide();

    t0 = steady_clock::now();
    double next_word_time = duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
    double next_input_time = next_word_time;

    size_t skipped = 0;
    size_t wn_in_line = 0;
    std::string current_word;
    int current_orp = 0;

    // Read lines from stdin
    std::string line;
    while (std::getline(std::cin, line)) {
        // keep last two lines for context
        lastlines[1] = lastlines[0];
        lastlines[0] = line;

        // tokenization
        std::vector<std::string> words = split_tokens(line);

        if (g_multiword) {
            // Join adjacent short words (<=3 chars) like Perl
            for (size_t i = 0; i + 1 < words.size();) {
                if (words[i].size() <= 3 && words[i+1].size() <= 3) {
                    words[i] = words[i] + " " + words[i+1];
                    words.erase(words.begin() + (ptrdiff_t)(i+1));
                } else {
                    ++i;
                }
            }
        }

        wn_in_line = 0;

        // inner "frame loop" for this line
        while (!words.empty()) {
            if (g_sigint) { print_stats_and_resume_hint(); return 0; }

            // Resume skipping
            if (skipped < (size_t)g_resume) {
                ++skipped;
                words.erase(words.begin());
                continue;
            }

            double now = duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();

            // Advance word on schedule (if not paused)
            if (!paused && next_word_time <= now) {
                current_word = words.front();
                words.erase(words.begin());
                current_orp = find_ORP(current_word);

                next_word_time += word_time(current_word, g_wpm, wordcounter);
                ++wordcounter;
                lettercounter += current_word.size();
                ++wn_in_line;
            }

            // Poll keys 20x / second
            if (next_input_time <= now) {
                while (tty.ok && tty.key_pressed()) {
                    int ch = tty.getch();
                    if      (ch == '[') { g_wpm = std::max(1, (int)(g_wpm * 0.9)); }
                    else if (ch == ']') { g_wpm = std::max(1, (int)(g_wpm * 1.1)); }
                    else if (ch == ' ') {
                        paused = !paused;
                        if (paused) {
                            // print context (two lines) with highlight
                            // Move up one line to write context above the word line
                            std::cout << CR << CLR2EOL << UP1;
                            std::cout.flush();
                            print_context((int)wn_in_line, lastlines, current_word);
                            show_guide();
                            show_word(current_word, current_orp, g_wpm, paused);
                        } else {
                            next_word_time = now;
                        }
                    }
                }
                next_input_time += 0.05; // 20 fps for input checks
            }

            // Redraw word every "frame" (reflect wpm/paused immediately)
            show_word(current_word, current_orp, g_wpm, paused);

            // Sleep until next event (word change or input poll)
            double next_event = paused ? next_input_time : std::min(next_word_time, next_input_time);
            double sleep_s = std::max(0.0, next_event - now);
            if (sleep_s > 0.0) {
                // usleep is fine for sub-second sleeps
                useconds_t us = (useconds_t)(sleep_s * 1e6);
                usleep(us);
            }
        }
    }

    // End stats
    auto elapsed = duration_cast<duration<double>>(steady_clock::now() - t0).count();
    double truewpm = (elapsed > 0.0) ? (wordcounter / elapsed * 60.0) : 0.0;
    std::cout << "\n ";
    std::cout.setf(std::ios::fixed); std::cout.precision(2);
    std::cout << elapsed << "s, " << wordcounter << " words, " << lettercounter
              << " letters, " << "\033[1m\033[32m" << truewpm << "\033[0m" << " true wpm\n";
    std::cout.flush();
    usleep(300000);
    return 0;
}
