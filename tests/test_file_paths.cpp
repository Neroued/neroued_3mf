#include "neroued/3mf/builder.h"
#include "neroued/3mf/writer.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace n3mf = neroued_3mf;
namespace fs = std::filesystem;

namespace {

// The observer must be able to inspect and clean long paths independently of the writer.
fs::path ObservationPath(const fs::path &absolute_path) {
#if defined(_WIN32)
    const auto &name = absolute_path.native();
    if (name.rfind(L"\\\\?\\", 0) == 0) { return absolute_path; }
    if (name.rfind(L"\\\\", 0) == 0) { return fs::path(L"\\\\?\\UNC\\" + name.substr(2)); }
    return fs::path(L"\\\\?\\" + name);
#else
    return absolute_path;
#endif
}

class WriterFilePaths : public testing::Test {
  protected:
    fs::path root_ = fs::current_path() /
                     ("n3mf-path-test-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(ObservationPath(root_), ec);
        EXPECT_FALSE(ec) << ec.message();
    }

    void CheckWriteAndReplace(const fs::path &input, const fs::path &absolute_output) {
        n3mf::WriteOptions opts;
        opts.deterministic = true;
        opts.compression = n3mf::WriteOptions::Compression::Store;
        for (const auto &label : {"First document", "Replacement document"}) {
            n3mf::Mesh mesh;
            mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}};
            mesh.triangles = {{0, 1, 2}};
            n3mf::DocumentBuilder builder;
            auto object = builder.AddMeshObject(label, std::move(mesh));
            builder.AddBuildItem(object);
            auto doc = builder.Build();
            const auto expected = n3mf::WriteToBuffer(doc, opts);

            ASSERT_NO_THROW(n3mf::WriteToFile(input, doc, opts));
            std::ifstream stream(ObservationPath(absolute_output), std::ios::binary);
            ASSERT_TRUE(stream.is_open());
            const std::vector<uint8_t> actual((std::istreambuf_iterator<char>(stream)),
                                              std::istreambuf_iterator<char>());
            EXPECT_EQ(actual, expected);
            stream.close();

            std::size_t files = 0;
            for (const auto &entry :
                 fs::directory_iterator(ObservationPath(absolute_output.parent_path()))) {
                EXPECT_EQ(entry.path().filename(), absolute_output.filename());
                ++files;
            }
            EXPECT_EQ(files, 1u); // No temporary output remains after replacement.
        }
    }

    fs::path LongOutput(bool unicode) const {
        auto directory = root_;
        const auto segment = unicode ? fs::path(u8"\u6d4b\u8bd5\u76ee\u5f55-1234567890abcdef")
                                     : fs::path("nested-directory-1234567890abcdef");
        while (directory.native().size() < 310) { directory /= segment; }
        return directory /
               (unicode ? fs::path(u8"\u5f69\u8272\u6a21\u578b.3mf") : fs::path("model.3mf"));
    }
};

TEST_F(WriterFilePaths, UnicodeFilename) {
    const auto output = root_ / fs::path(u8"\u5f69\u8272\u6a21\u578b-\U0001f431.3mf");
    CheckWriteAndReplace(output, output);
}

TEST_F(WriterFilePaths, RelativePath) {
    const auto output = root_ / "relative.3mf";
    CheckWriteAndReplace(fs::path(".") / root_.filename() / "relative.3mf", output);
}

#if defined(_WIN32)
TEST_F(WriterFilePaths, LongAsciiPath) {
    const auto output = LongOutput(false);
    ASSERT_GT(output.native().size(), 260u);
    CheckWriteAndReplace(output, output);
}

TEST_F(WriterFilePaths, LongUnicodePath) {
    const auto output = LongOutput(true);
    ASSERT_GT(output.native().size(), 260u);
    CheckWriteAndReplace(output, output);
}

TEST_F(WriterFilePaths, LongRelativePath) {
    const auto output = LongOutput(true);
    const auto relative = output.lexically_relative(fs::current_path());
    ASSERT_FALSE(relative.is_absolute());
    CheckWriteAndReplace(relative, output);
}

TEST_F(WriterFilePaths, AlreadyExtendedPath) {
    const auto output = LongOutput(true);
    CheckWriteAndReplace(ObservationPath(output), output);
}

TEST_F(WriterFilePaths, DeviceNamespacePath) {
    const auto output = root_ / "device-path.3mf";
    const auto &name = output.native();
    if (name.rfind(L"\\\\", 0) == 0) { GTEST_SKIP() << "Requires a local drive"; }
    CheckWriteAndReplace(fs::path(L"\\\\.\\" + name), output);
}
#endif

} // namespace
