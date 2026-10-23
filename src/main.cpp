#include "qmyz/core.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#ifndef QMYZ_ENTRY
#define QMYZ_ENTRY "qmyz"
#endif
using namespace qmyz;
namespace {
int integer(const std::string &s) {
    std::size_t pos = 0;
    int n = std::stoi(s, &pos);
    if (pos != s.size())
        throw std::runtime_error("整数参数格式错误");
    return n;
}
void help() {
    std::cout << R"(QMYZ C++ 1.0 — GPL-3.0
用法：qmyz <answer|interactive|courses|fight|check-bank> [选项]
      qm_cli [选项] / qm_auto [选项] / fight [选项]
  -u, --url URL        登录链接（推荐改用 QMYZ_LOGIN_URL 环境变量）
  -i, --course ID      课程ID
  -c, --csv PATH       CSV题库路径（默认 qmyz/<课程ID>.csv）
  -n, --num N          answer: 实际提交题数；fight: 对战场数（必须>0）
  -a, --accuracy N     目标准确率 0–100，默认100，不保证实际精确达到
      --delay-min N   请求间隔下限（秒），默认3
      --delay-max N   请求间隔上限（秒），默认6
      --max-skips N   连续无提交上限，默认100
      --rounds N      每场对战最多回合数，默认6（与原程序相同）
      --summary PATH  写入不含凭据的JSON统计（answer模式）
      --help          显示帮助
示例：QMYZ_LOGIN_URL='...' ./build/qmyz answer -i 9 -n 300 -a 100
      ./build/qmyz check-bank -c qmyz/9.csv
未知题目不计数；网络错误/平台验证/会话失效会停止，不无限重试。
)";
}
} // namespace
int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    try {
        Options o;
        const char *env = std::getenv("QMYZ_LOGIN_URL");
        if (env)
            o.url = env;
        std::string entry = QMYZ_ENTRY, mode = entry == "qm_auto" ? "interactive"
                                               : entry == "fight" ? "fight"
                                                                  : "answer";
        int first = 1;
        if (argc > 1 && argv[1][0] != '-') {
            mode = argv[1];
            first = 2;
        }
        if (argc == 1 && entry == "qmyz") {
            help();
            return 0;
        }
        for (int i = first; i < argc; ++i) {
            std::string k = argv[i];
            if (k == "--help" || k == "-h") {
                help();
                return 0;
            }
            if (i + 1 >= argc)
                throw std::runtime_error("选项缺少参数");
            std::string v = argv[++i];
            if (k == "-u" || k == "--url")
                o.url = v;
            else if (k == "-i" || k == "--course")
                o.course = v;
            else if (k == "-c" || k == "--csv")
                o.csv = v;
            else if (k == "-n" || k == "--num")
                o.count = integer(v);
            else if (k == "-a" || k == "--accuracy")
                o.accuracy = integer(v);
            else if (k == "--delay-min")
                o.delay_min = integer(v);
            else if (k == "--delay-max")
                o.delay_max = integer(v);
            else if (k == "--max-skips")
                o.max_skips = integer(v);
            else if (k == "--rounds")
                o.rounds = integer(v);
            else if (k == "--summary")
                o.summary = v;
            else
                throw std::runtime_error("未知命令行选项");
        }
        if (mode != "answer" && mode != "interactive" && mode != "fight" && mode != "courses" &&
            mode != "check-bank")
            throw std::runtime_error("未知运行模式");
        if (o.accuracy < 0 || o.accuracy > 100 || o.delay_min < 0 || o.delay_max < o.delay_min ||
            o.max_skips <= 0 || o.rounds < 1 || o.rounds > 100)
            throw std::runtime_error("参数范围错误");
        auto bank_path = [&]() {
            if (o.csv.empty()) {
                if (o.course.empty() ||
                    o.course.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("请指定数字课程ID或题库路径");
                o.csv = "qmyz/" + o.course + ".csv";
            }
        };
        if (mode == "check-bank") {
            bank_path();
            Bank bank(o.csv);
            std::cout << "题库有效：" << bank.size() << "条" << std::endl;
            return 0;
        }
        if (mode == "interactive" && o.url.empty()) {
            std::cerr << "请输入登录URL（终端可能回显，建议环境变量）：";
            std::getline(std::cin, o.url);
        }
        if (o.url.empty())
            throw std::runtime_error("请设置QMYZ_LOGIN_URL或-u登录链接");
        // Validate local configuration before contacting the platform.
        if (mode == "answer" || mode == "fight") {
            if (o.count <= 0 || o.count > 100000)
                throw std::runtime_error("请指定1–100000的目标数量");
            if (o.course.empty() || o.course.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("课程ID必须为数字");
            bank_path();
        }
        if (mode == "interactive" || mode == "courses") {
            Client listing(o);
            for (auto &item : listing.courses())
                std::cout << item.first << "\t" << item.second << "\t"
                          << (std::filesystem::exists("qmyz/" + item.first + ".csv") ? "[有题库]"
                                                                                     : "[无题库]")
                          << std::endl;
            if (mode == "courses")
                return 0;
            std::string s;
            std::cout << "课程ID：";
            std::getline(std::cin, o.course);
            std::cout << "实际提交题数：";
            std::getline(std::cin, s);
            o.count = integer(s);
            std::cout << "目标准确率（默认100）：";
            std::getline(std::cin, s);
            o.accuracy = s.empty() ? 100 : integer(s);
            if (o.count <= 0 || o.count > 100000 || o.accuracy < 0 || o.accuracy > 100 ||
                o.course.empty() || o.course.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("交互参数范围错误");
            bank_path();
        }
        Bank bank(o.csv, mode == "fight");
        Client client(o);
        if (mode == "fight") {
            client.combat(bank, o.count);
            return 0;
        }
        auto report = [&](const Stats &s) {
            std::cout << "实际提交进度：" << s.submitted() << "/" << o.count << "；答对"
                      << s.correct << "；答错" << s.wrong << "；跳过" << s.skipped << "；正确率"
                      << std::fixed << std::setprecision(4) << s.accuracy() << "%" << std::endl;
            if (!o.summary.empty()) {
                std::ofstream out(o.summary);
                if (!out)
                    throw std::runtime_error("无法写入统计文件");
                out << s.json().dump(2);
                out.flush();
                if (!out)
                    throw std::runtime_error("统计文件写入失败");
            }
        };
        auto stats = run_exact(
            o.count, o.max_skips, [&](const Stats &s) { return client.answer(bank, s); }, report);
        std::cout << "任务完成：实际提交" << stats.submitted() << "题；正确率" << stats.accuracy()
                  << "%" << std::endl;
        return 0;
    } catch (const nlohmann::json::exception &) {
        std::cerr << "错误：平台数据结构与预期不符（未输出响应或凭据）" << std::endl;
        return 1;
    } catch (const std::exception &e) {
        // All application errors are controlled messages, but dependencies can include data.
        std::string msg = e.what();
        if (msg.find("://") != std::string::npos || msg.find("verify_request") != std::string::npos)
            msg = "发生错误（敏感详情已隐藏）";
        std::cerr << "错误：" << msg << std::endl;
        return 1;
    }
}
