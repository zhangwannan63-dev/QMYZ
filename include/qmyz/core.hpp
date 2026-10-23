#pragma once
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>
namespace qmyz {
using Json = nlohmann::json;
using Row = std::vector<std::string>;
std::vector<Row> read_csv(const std::string &path);
void write_csv_row(std::ostream &out, const Row &row);
std::string decrypt(const std::string &value,
                    const std::string &key = "ZDBmMTNiZGI3MDRhMWVhMWE3MTcwNjJiNTk0NzY0ODg");
std::string origin_of(const std::string &url);
std::string scalar(const Json &j);
Json extract_subject(const std::string &html);
struct Bank {
    explicit Bank(std::string path, bool create = false);
    std::string by_text(const std::string &text) const;
    std::string by_id(const std::string &course, const std::string &id) const;
    bool learn(const Json &subject, const std::string &answer);
    std::size_t size() const { return rows.size(); }
    std::string path;
    std::vector<Row> rows;
    std::unordered_map<std::string, std::string> text_index, id_index;
};
enum class Result { correct, wrong, skipped };
Result classify(const Json &response);
struct Stats {
    int correct = 0, wrong = 0, attempts = 0, skipped = 0;
    int submitted() const { return correct + wrong; }
    double accuracy() const { return submitted() ? 100.0 * correct / submitted() : 0; }
    void add(Result result);
    Json json() const;
};
Stats run_exact(int target, int max_skips, const std::function<Result(const Stats &)> &step,
                const std::function<void(const Stats &)> &progress);
struct HttpResponse {
    long status = 0;
    std::string body, location;
};
class Http {
  public:
    explicit Http(const std::string &url);
    ~Http();
    Http(const Http &) = delete;
    Http &operator=(const Http &) = delete;
    void login(const std::string &url);
    std::string get(const std::string &path);
    Json post(const std::string &path,
              const std::vector<std::pair<std::string, std::string>> &fields);
    const std::string origin;

  private:
    void *handle = nullptr;
    HttpResponse request(const std::string &url,
                         const std::vector<std::pair<std::string, std::string>> *fields);
};
struct Options {
    std::string url, course, csv, summary;
    int count = 0, accuracy = 100, delay_min = 3, delay_max = 6, max_skips = 100, rounds = 6;
};
class Client {
  public:
    explicit Client(const Options &options);
    std::vector<std::pair<std::string, std::string>> courses();
    Result answer(Bank &bank, const Stats &stats);
    void combat(Bank &bank, int matches);

  private:
    Options options;
    Http http;
    void pause() const;
};
} // namespace qmyz
