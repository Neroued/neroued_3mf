// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

/// \file generate_test_3mf.cpp
/// \brief Generate a test 3MF file with watermark payload for verifying n3mf_detect.

#include <cstdio>
#include <cstdlib>
#include <neroued/3mf/builder.h>
#include <neroued/3mf/writer.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "Usage: %s <output.3mf> [--payload <hex|text>] [--key <hex>] [--text]\n"
                     "\n"
                     "Generate a test 3MF with embedded watermark.\n"
                     "  --payload <val>  Payload to embed (hex, or text with --text)\n"
                     "  --key <hex>      Encryption key (hex). Omit for unencrypted.\n"
                     "  --text           Treat --payload as UTF-8 text instead of hex\n"
                     "\n"
                     "Default: payload=\"neroued_3mf\" (text), no key\n",
                     argv[0]);
        return 1;
    }

    std::string output_path;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> key;
    bool text_mode = false;
    std::string payload_str;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--text") {
            text_mode = true;
            continue;
        }
        if (arg == "--payload") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Error: --payload requires an argument\n");
                return 1;
            }
            payload_str = argv[++i];
            continue;
        }
        if (arg == "--key") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Error: --key requires a hex argument\n");
                return 1;
            }
            if (!ParseHex(argv[++i], key)) {
                std::fprintf(stderr, "Error: invalid hex key\n");
                return 1;
            }
            continue;
        }
        if (output_path.empty()) {
            output_path = arg;
        } else {
            std::fprintf(stderr, "Error: unexpected argument '%s'\n", argv[i]);
            return 1;
        }
    }

    if (output_path.empty()) {
        std::fprintf(stderr, "Error: no output path\n");
        return 1;
    }

    if (payload_str.empty()) {
        std::string default_text = "neroued_3mf";
        payload.assign(default_text.begin(), default_text.end());
    } else if (text_mode) {
        payload.assign(payload_str.begin(), payload_str.end());
    } else {
        if (!ParseHex(payload_str, payload)) {
            std::fprintf(stderr, "Error: invalid hex payload\n");
            return 1;
        }
    }

    namespace n3mf = neroued_3mf;

    n3mf::DocumentBuilder builder;
    builder.SetUnit(n3mf::Unit::Millimeter);
    builder.AddMetadata("Application", "n3mf_generate_test");

    n3mf::Mesh mesh;
    mesh.vertices.reserve(1002);
    mesh.vertices.push_back({0, 0, 0});
    mesh.vertices.push_back({1, 0, 0});
    for (int i = 0; i < 1000; ++i) {
        mesh.vertices.push_back({0.5f, static_cast<float>(i + 1), 0});
    }
    mesh.triangles.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        mesh.triangles.push_back({0, 1, static_cast<uint32_t>(i + 2)});
    }

    auto obj = builder.AddMeshObject("TestMesh", std::move(mesh));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();

    n3mf::WriteOptions opts;
    opts.watermark.payload = payload;
    opts.watermark.key = key;
    opts.watermark.repetition = 3;

    n3mf::WriteToFile(output_path, doc, opts);
    std::fprintf(stdout, "Written: %s (payload %zu bytes, key %zu bytes)\n", output_path.c_str(),
                 payload.size(), key.size());
    return 0;
}
