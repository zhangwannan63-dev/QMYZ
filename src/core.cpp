#include "qmyz/core.hpp"
#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <openssl/evp.h>
#include <sstream>
#include <stdexcept>
namespace qmyz {
std::string scalar(const Json &j) { return j.is_string() ? j.get<std::string>() : j.dump(); }
std::vector<Row> read_csv(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("无法读取题库文件");
    std::string data((std::istreambuf_iterator<char>(in)), {});
    if (data.compare(0, 3, "\xef\xbb\xbf") == 0)
        data.erase(0, 3);
    std::vector<Row> rows;
    Row row;
    std::string field;
    bool quoted = false, closed = false, started = false;
    for (std::size_t i = 0; i < data.size(); ++i) {
        char c = data[i];
        started = true;
        if (quoted) {
            if (c == '"') {
                if (i + 1 < data.size() && data[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                    closed = true;
                }
            } else
                field += c;
        } else if (c == ',' || c == '\r' || c == '\n') {
            row.push_back(field);
            field.clear();
            closed = false;
            if (c != ',') {
                rows.push_back(row);
                row.clear();
                started = false;
                if (c == '\r' && i + 1 < data.size() && data[i + 1] == '\n')
                    ++i;
            }
        } else if (c == '"' && field.empty() && !closed)
            quoted = true;
        else {
            if (closed || c == '"')
                throw std::runtime_error("CSV引号格式错误");
            field += c;
        }
    }
    if (quoted)
        throw std::runtime_error("CSV引号未闭合");
    if (started || !row.empty() || !field.empty()) {
        row.push_back(field);
        rows.push_back(row);
    }
    return rows;
}
void write_csv_row(std::ostream &out, const Row &row) {
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (i)
            out << ',';
        out << '"';
        for (char c : row[i]) {
            out << c;
            if (c == '"')
                out << '"';
        }
        out << '"';
    }
    out << '\n';
}
static std::vector<unsigned char> b64(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }),
            s.end());
    if (s.empty() || s.size() % 4 == 1)
        throw std::runtime_error("无效Base64");
    while (s.size() % 4)
        s += '=';
    auto pad = s.size() - s.find_last_not_of('=') - 1;
    if (pad > 2)
        throw std::runtime_error("无效Base64填充");
    for (std::size_t i = 0; i < s.size() - pad; ++i)
        if (!std::isalnum(static_cast<unsigned char>(s[i])) && s[i] != '+' && s[i] != '/')
            throw std::runtime_error("无效Base64字符");
    std::vector<unsigned char> out(s.size());
    int n = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char *>(s.data()),
                            static_cast<int>(s.size()));
    if (n < 0)
        throw std::runtime_error("Base64解码失败");
    out.resize(n - pad);
    return out;
}
std::string decrypt(const std::string &value, const std::string &key) {
    auto data = b64(value), k = b64(key);
    const EVP_CIPHER *cipher = nullptr;
    if (k.size() == 16)
        cipher = EVP_aes_128_ecb();
    else if (k.size() == 24)
        cipher = EVP_aes_192_ecb();
    else if (k.size() == 32)
        cipher = EVP_aes_256_ecb();
    if (!cipher || data.empty() || data.size() % 16)
        throw std::runtime_error("AES参数无效");
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),
                                                                        EVP_CIPHER_CTX_free);
    std::vector<unsigned char> out(data.size() + 16);
    int n = 0, last = 0;
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, k.data(), nullptr) != 1 ||
        EVP_DecryptUpdate(ctx.get(), out.data(), &n, data.data(), data.size()) != 1 ||
        EVP_DecryptFinal_ex(ctx.get(), out.data() + n, &last) != 1)
        throw std::runtime_error("AES解密或PKCS7校验失败");
    return std::string(reinterpret_cast<char *>(out.data()), n + last);
}
std::string origin_of(const std::string &url) {
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> u(curl_url(), curl_url_cleanup);
    if (!u || curl_url_set(u.get(), CURLUPART_URL, url.c_str(), 0) != CURLUE_OK)
        throw std::runtime_error("登录链接格式错误");
    auto part = [&](CURLUPart p) {
        char *raw = nullptr;
        std::string s;
        if (curl_url_get(u.get(), p, &raw, 0) == CURLUE_OK) {
            s = raw;
            curl_free(raw);
        }
        return s;
    };
    auto scheme = part(CURLUPART_SCHEME), host = part(CURLUPART_HOST), port = part(CURLUPART_PORT);
    if ((scheme != "http" && scheme != "https") || host.empty() || !part(CURLUPART_USER).empty() ||
        !part(CURLUPART_PASSWORD).empty())
        throw std::runtime_error("仅允许无内嵌密码的HTTP(S)链接");
    return scheme + "://" + host + (port.empty() ? "" : ":" + port);
}
Json extract_subject(const std::string &html) {
    for (std::size_t begin = html.find('{'); begin != std::string::npos;
         begin = html.find('{', begin + 1)) {
        int depth = 0;
        bool str = false, escape = false;
        for (std::size_t i = begin; i < html.size(); ++i) {
            char c = html[i];
            if (str) {
                if (escape)
                    escape = false;
                else if (c == '\\')
                    escape = true;
                else if (c == '"')
                    str = false;
            } else if (c == '"')
                str = true;
            else if (c == '{')
                ++depth;
            else if (c == '}' && --depth == 0) {
                auto j = Json::parse(html.substr(begin, i - begin + 1), nullptr, false);
                if (!j.is_discarded() && j.is_object() && j.contains("courseId") &&
                    j.contains("subDescript"))
                    return j;
                break;
            }
        }
    }
    throw std::runtime_error("对战页面未找到题目JSON");
}
static const Row header = {"courseId", "id",      "subType", "optionCount", "subDescript",
                           "option0",  "option1", "option2", "option3",     "answer"};
Bank::Bank(std::string file, bool create) : path(std::move(file)) {
    if (!std::filesystem::exists(path) && create) {
        auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
        std::ofstream out(path);
        if (!out)
            throw std::runtime_error("无法创建题库");
        write_csv_row(out, header);
    }
    for (auto &r : read_csv(path)) {
        if (r == header || (r.size() == 1 && r[0].empty()))
            continue;
        if (r.size() != 10)
            throw std::runtime_error("CSV必须为10列");
        if (r.back().empty())
            continue;
        rows.push_back(r);
        text_index.emplace(r[4], r.back());
        id_index.emplace(r[0] + ":" + r[1], r.back());
    }
}
std::string Bank::by_text(const std::string &text) const {
    auto it = text_index.find(text);
    return it == text_index.end() ? "" : it->second;
}
std::string Bank::by_id(const std::string &course, const std::string &id) const {
    auto it = id_index.find(course + ":" + id);
    return it == id_index.end() ? "" : it->second;
}
bool Bank::learn(const Json &subject, const std::string &answer) {
    if (answer.empty() || answer.find_first_not_of("ABCD") != std::string::npos)
        throw std::runtime_error("平台正确答案格式错误");
    auto course = scalar(subject.at("courseId")),
         id = scalar(subject.at(subject.contains("subjectId") ? "subjectId" : "id"));
    if (!by_id(course, id).empty())
        return false;
    int count = std::stoi(scalar(subject.at("optionCount")));
    if (count < 1 || count > 4)
        throw std::runtime_error("不支持该选项数量");
    Row r = {course, id, scalar(subject.at("subType")), std::to_string(count),
             decrypt(subject.at("subDescript").get<std::string>())};
    for (int i = 0; i < 4; ++i)
        r.push_back(i < count ? decrypt(subject.at("option" + std::to_string(i)).get<std::string>())
                              : "");
    r.push_back(answer);
    std::ofstream out(path, std::ios::app);
    if (!out)
        throw std::runtime_error("无法写入题库");
    write_csv_row(out, r);
    out.flush();
    if (!out)
        throw std::runtime_error("保存题库失败");
    rows.push_back(r);
    text_index.emplace(r[4], answer);
    id_index.emplace(course + ":" + id, answer);
    return true;
}
Result classify(const Json &r) {
    auto msg = r.value("message", std::string{});
    if (msg == "回答正确！")
        return Result::correct;
    if (msg.find("回答错误") != std::string::npos || msg.find("回答不正确") != std::string::npos)
        return Result::wrong;
    return Result::skipped;
}
void Stats::add(Result r) {
    ++attempts;
    if (r == Result::correct)
        ++correct;
    else if (r == Result::wrong)
        ++wrong;
    else
        ++skipped;
}
Json Stats::json() const {
    return {{"submitted", submitted()}, {"correct", correct}, {"wrong", wrong},
            {"attempts", attempts},     {"skipped", skipped}, {"accuracy", accuracy()}};
}
Stats run_exact(int target, int max_skips, const std::function<Result(const Stats &)> &step,
                const std::function<void(const Stats &)> &progress) {
    if (target <= 0 || max_skips <= 0)
        throw std::runtime_error("目标题数和跳过上限必须为正数");
    Stats s;
    int consecutive = 0;
    while (s.submitted() < target) {
        auto r = step(s);
        s.add(r);
        progress(s);
        consecutive = r == Result::skipped ? consecutive + 1 : 0;
        if (consecutive >= max_skips)
            throw std::runtime_error("连续未提交达到上限，任务未完成");
    }
    return s;
}
} // namespace qmyz
