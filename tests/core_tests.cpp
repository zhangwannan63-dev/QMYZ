#include "qmyz/core.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/evp.h>
#include <sstream>
#include <stdexcept>
using namespace qmyz;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (...) {
        failed = true;
    }
    check(failed, "expected rejection");
}
std::string encrypt_test(const std::string &s) {
    const std::string key = "d0f13bdb704a1ea1a717062b59476488";
    auto *ctx = EVP_CIPHER_CTX_new();
    std::vector<unsigned char> out(s.size() + 32);
    int n = 0, last = 0;
    EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr,
                       reinterpret_cast<const unsigned char *>(key.data()), nullptr);
    EVP_EncryptUpdate(ctx, out.data(), &n, reinterpret_cast<const unsigned char *>(s.data()),
                      s.size());
    EVP_EncryptFinal_ex(ctx, out.data() + n, &last);
    EVP_CIPHER_CTX_free(ctx);
    std::string result(4 * ((n + last + 2) / 3), '\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char *>(result.data()), out.data(), n + last);
    return result;
}
int main() {
    try {
        auto path = (std::filesystem::temp_directory_path() / "qmyz-cpp-core-test.csv").string();
        {
            std::ofstream out(path, std::ios::binary);
            out << "\xef\xbb\xbf";
            write_csv_row(out, {"a,b", "line1\nline2", "中文\"引号", ""});
        }
        auto rows = read_csv(path);
        check(rows.size() == 1 && rows[0] == Row({"a,b", "line1\nline2", "中文\"引号", ""}),
              "CSV round trip");
        {
            std::ofstream out(path);
            out << "\"broken";
        }
        rejects([&] { read_csv(path); });
        std::filesystem::remove(path);
        Bank bank(path, true);
        auto ciphertext = encrypt_test("中文题目,含逗号\n第二行");
        check(decrypt(ciphertext) == "中文题目,含逗号\n第二行", "AES UTF8 round trip");
        while (!ciphertext.empty() && ciphertext.back() == '=')
            ciphertext.pop_back();
        check(!decrypt(ciphertext).empty(), "missing base64 padding");
        rejects([] { decrypt("!"); });
        rejects([] { decrypt("AAAA"); });
        Json subject = {{"courseId", 9},
                        {"subjectId", 42},
                        {"subType", "单选题"},
                        {"optionCount", 2},
                        {"subDescript", encrypt_test("中文题目")},
                        {"option0", encrypt_test("选项甲")},
                        {"option1", encrypt_test("选项乙")}};
        check(bank.learn(subject, "AB"), "learn new");
        check(!bank.learn(subject, "AB"), "deduplicate");
        Bank loaded(path);
        check(loaded.by_text("中文题目") == "AB" && loaded.by_id("9", "42") == "AB",
              "bank indexes");
        std::filesystem::remove(path);
        check(origin_of("http://localhost:8080/a?secret=x") == "http://localhost:8080", "origin");
        rejects([] { origin_of("file:///tmp/x"); });
        rejects([] { origin_of("http://user:pass@localhost/a"); });
        auto extracted = extract_subject("<script>var x={\"other\":1};var q=" + subject.dump() +
                                         ";var y={\"x\":2};</script>");
        check(extracted == subject, "HTML balanced extraction");
        check(classify({{"message", "回答正确！"}}) == Result::correct, "correct classification");
        check(classify({{"message", "回答错误！"}}) == Result::wrong, "wrong classification");
        check(classify({{"message", "登录失效"}}) == Result::skipped, "reject non answer");
        int calls = 0;
        auto stats = run_exact(
            5, 10,
            [&](const Stats &) {
                ++calls;
                return calls % 3 == 0 ? Result::skipped : Result::correct;
            },
            [](const Stats &) {});
        check(stats.submitted() == 5 && calls == 7 && stats.skipped == 2, "exact confirmed count");
        calls = 0;
        rejects([&] {
            run_exact(
                1, 3,
                [&](const Stats &) {
                    ++calls;
                    return Result::skipped;
                },
                [](const Stats &) {});
        });
        check(calls == 3, "bounded skips");
        rejects([] {
            run_exact(0, 3, [](const Stats &) { return Result::correct; }, [](const Stats &) {});
        });
        std::cout << "All core tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
