// wallet_tool.cpp
#include <db_cxx.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Helper: uppercase hex formatting of 5 bytes as WDK string
std::string format_wdk(const unsigned char* data, size_t len = 5) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    for(size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << (int)data[i];
    }
    return oss.str();
}

struct Options {
    std::string wallet_path;
    bool extract_key = false;
    bool verbose = false;
    std::string output_file;
};

// Extract WDK key from Berkeley DB wallet
bool extract_wdk(const std::string& wallet_path, std::string& out_wdk, bool verbose) {
    try {
        Db db(nullptr, 0);
        db.set_flags(DB_READ_UNCOMMITTED);
        db.open(nullptr, wallet_path.c_str(), nullptr, DB_BTREE, DB_RDONLY, 0);

        Dbc* cursorp;
        db.cursor(nullptr, &cursorp, 0);

        Dbt key, data;

        if(verbose) std::cout << "[Info] Starting wallet scan for WDK...\n";

        bool found = false;
        unsigned char candidate[5] = {0};

        while(cursorp->get(&key, &data, DB_NEXT) == 0) {
            if(data.get_size() >= 5) {
                unsigned char* buf = static_cast<unsigned char*>(data.get_data());
                for(u_int32_t i = 0; i <= data.get_size() - 5; i++) {
                    unsigned char* ptr = buf + i;

                    // Simple entropy check: not all zero and not all same byte
                    bool all_zero = true;
                    bool all_same = true;
                    for(int j = 1; j < 5; j++) {
                        if(ptr[j] != ptr[0]) all_same = false;
                        if(ptr[j] != 0) all_zero = false;
                    }
                    if(ptr[0] != 0) all_zero = false;

                    if(!all_zero && !all_same) {
                        memcpy(candidate, ptr, 5);
                        found = true;
                        if(verbose) std::cout << "[Info] Candidate WDK found at offset " << i << "\n";
                        break;
                    }
                }
            }
            if(found) break;
        }

        cursorp->close();
        db.close(0);

        if(!found) {
            if(verbose) std::cout << "[Error] No WDK candidate found in wallet.dat\n";
            return false;
        }

        out_wdk = format_wdk(candidate, 5);
        return true;

    } catch(DbException &e) {
        std::cerr << "[Error] Berkeley DB Exception: " << e.what() << std::endl;
        return false;
    } catch(std::exception &e) {
        std::cerr << "[Error] Exception: " << e.what() << std::endl;
        return false;
    }
}

void print_usage() {
    std::cout <<
        "Usage:\n"
        "  --wallet <path>          Specify wallet.dat file path\n"
        "  --extract-key            Extract and display the unique key\n"
        "  --verbose                Enable detailed output\n"
        "  --output=<file>          Save results to specified file\n"
        "  --help                  Show this help\n"
        << std::endl;
}

int main(int argc, char* argv[]) {
    Options opts;

    if(argc < 2) {
        print_usage();
        return 1;
    }

    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(arg == "--help") {
            print_usage();
            return 0;
        } else if(arg == "--extract-key") {
            opts.extract_key = true;
        } else if(arg == "--verbose") {
            opts.verbose = true;
        } else if(arg.substr(0,9) == "--output=") {
            opts.output_file = arg.substr(9);
        } else if(arg == "--wallet") {
            if(i + 1 < argc) {
                opts.wallet_path = argv[++i];
            } else {
                std::cerr << "[Error] --wallet requires a path argument\n";
                return 1;
            }
        } else {
            std::cerr << "[Warning] Unknown option: " << arg << "\n";
        }
    }

    if(opts.wallet_path.empty()) {
        std::cerr << "[Error] Wallet path not specified. Use --wallet <path>\n";
        return 1;
    }

    if(opts.verbose) std::cout << "[Verbose] Wallet path: " << opts.wallet_path << std::endl;

    if(!opts.extract_key) {
        std::cerr << "[Error] No operation specified. Use --extract-key\n";
        return 1;
    }

    std::string wdk;
    if(!extract_wdk(opts.wallet_path, wdk, opts.verbose)) {
        std::cerr << "[Error] WDK extraction failed\n";
        return 1;
    }

    std::string output_line = "[Final key] WDK: " + wdk;
    std::cout << output_line << std::endl;

    if(!opts.output_file.empty()) {
        std::ofstream ofs(opts.output_file);
        if(ofs) {
            ofs << output_line << std::endl;
            ofs.close();
            if(opts.verbose)
                std::cout << "[Verbose] Output saved to " << opts.output_file << std::endl;
        } else {
            std::cerr << "[Error] Failed to open output file\n";
        }
    }

    return 0;
}
