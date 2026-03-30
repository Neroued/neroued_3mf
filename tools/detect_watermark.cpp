// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

/// \file detect_watermark.cpp
/// \brief CLI tool: detect L2 library fingerprint and decode L1 watermark payload from a 3MF file.
///
/// Usage:
///   n3mf_detect <file.3mf> [options]
///
/// Options:
///   --key <hex>    Decryption key (hex-encoded). Omit for unencrypted payloads.
///   --raw          Write decoded payload as raw bytes to stdout (for piping).
///   --help, -h     Show usage.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <neroued/3mf/watermark.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

void PrintUsage(const char *prog) {
    std::fprintf(stderr,
                 "Usage: %s <file.3mf> [options]\n"
                 "\n"
                 "Detect watermarks in a 3MF file produced by neroued_3mf.\n"
                 "\n"
                 "Options:\n"
                 "  --key <hex>  Decryption key (hex-encoded, e.g. aabb0102)\n"
                 "  --raw        Output decoded payload as raw bytes to stdout\n"
                 "  --help, -h   Show this help\n",
                 prog);
}

bool ParseHex(std::string_view hex, std::vector<uint8_t> &out) {
    if (hex.size() % 2 != 0) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nibble(hex[i]);
        int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

std::string ToHex(const std::vector<uint8_t> &data) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(data.size() * 2);
    for (uint8_t b : data) {
        s.push_back(kHex[(b >> 4) & 0x0f]);
        s.push_back(kHex[b & 0x0f]);
    }
    return s;
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string file_path;
    std::vector<uint8_t> key;
    bool raw_output = false;
    bool has_key = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }
        if (arg == "--raw") {
            raw_output = true;
            continue;
        }
        if (arg == "--key") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Error: --key requires a hex argument\n");
                return 1;
            }
            if (!ParseHex(argv[++i], key)) {
                std::fprintf(stderr, "Error: invalid hex key '%s'\n", argv[i]);
                return 1;
            }
            has_key = true;
            continue;
        }
        if (arg.starts_with("-")) {
            std::fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
        }
        if (file_path.empty()) {
            file_path = arg;
        } else {
            std::fprintf(stderr, "Error: unexpected argument '%s'\n", argv[i]);
            return 1;
        }
    }

    if (file_path.empty()) {
        std::fprintf(stderr, "Error: no input file specified\n");
        PrintUsage(argv[0]);
        return 1;
    }

    std::error_code ec;
    auto fsize = std::filesystem::file_size(file_path, ec);
    if (ec) {
        std::fprintf(stderr, "Error: cannot stat '%s': %s\n", file_path.c_str(),
                     ec.message().c_str());
        return 1;
    }

    std::ifstream in(file_path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "Error: cannot open '%s'\n", file_path.c_str());
        return 1;
    }

    std::vector<uint8_t> data(static_cast<std::size_t>(fsize));
    in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(fsize));
    if (!in) {
        std::fprintf(stderr, "Error: failed to read '%s'\n", file_path.c_str());
        return 1;
    }
    in.close();

    auto result = neroued_3mf::DetectWatermark(data, key);

    if (raw_output) {
        if (result.has_l1_payload && !result.payload.empty()) {
            std::fwrite(result.payload.data(), 1, result.payload.size(), stdout);
        }
        return result.has_l1_payload ? 0 : 1;
    }

    std::fprintf(stdout, "File:   %s (%zu bytes)\n", file_path.c_str(), data.size());

    std::fprintf(stdout, "L2 sig: %s\n", result.has_l2_signature ? "found" : "not found");

    if (!has_key && key.empty()) {
        std::fprintf(stdout, "L1:     decoding without key (unencrypted mode)\n");
    }

    if (result.has_l1_payload) {
        std::fprintf(stdout, "L1:     decoded (%zu bytes%s)\n", result.payload.size(),
                     result.payload_truncated ? ", truncated" : "");
        std::fprintf(stdout, "  hex:  %s\n", ToHex(result.payload).c_str());

        bool printable = true;
        for (uint8_t b : result.payload) {
            if (b < 0x20 && b != '\t' && b != '\n' && b != '\r') {
                printable = false;
                break;
            }
        }
        if (printable && !result.payload.empty()) {
            std::fprintf(stdout, "  text: %.*s\n", static_cast<int>(result.payload.size()),
                         reinterpret_cast<const char *>(result.payload.data()));
        }
    } else {
        std::fprintf(stdout, "L1:     no payload decoded\n");
    }

    return 0;
}
