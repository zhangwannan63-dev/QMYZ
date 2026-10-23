#include "qmyz/core.hpp"
#include <chrono>
#include <iostream>
#include <random>
#include <regex>
#include <stdexcept>
#include <thread>
namespace qmyz {
namespace {
constexpr const char *prefix = "/yiban-web/stu/";
std::mt19937 &rng() {
    static thread_local std::mt19937 engine(std::random_device{}());
    return engine;
}
bool guard_question(const std::string &text) {
    return text.find("刷题") != std::string::npos || text.find("请选择") != std::string::npos;
}
std::string answer_for(Bank &bank, const Json &subject) {
    return bank.by_id(scalar(subject.at("courseId")),
                      scalar(subject.at(subject.contains("subjectId") ? "subjectId" : "id")));
}
} // namespace
Client::Client(const Options &o) : options(o), http(o.url) {
    http.login(o.url);
    std::cout << "登录会话已获取（不输出凭据）" << std::endl;
}
void Client::pause() const {
    std::uniform_int_distribution<int> delay(options.delay_min, options.delay_max);
    std::this_thread::sleep_for(std::chrono::seconds(delay(rng())));
}
std::vector<std::pair<std::string, std::string>> Client::courses() {
    auto html = http.get(std::string(prefix) + "toCourse.jhtml");
    std::regex pattern(
        R"re(href\s*=\s*["'][^"']*toSubject\.jhtml\?courseId=(\d+)[^"']*["'][\s\S]*?<div[^>]*class\s*=\s*["'][^"']*mui-media-body[^"']*["'][^>]*>([\s\S]*?)</div>)re");
    std::vector<std::pair<std::string, std::string>> result;
    for (auto it = std::sregex_iterator(html.begin(), html.end(), pattern);
         it != std::sregex_iterator(); ++it) {
        auto name = std::regex_replace((*it)[2].str(), std::regex("<[^>]*>"), "");
        result.emplace_back((*it)[1].str(), name);
    }
    return result;
}
Result Client::answer(Bank &bank, const Stats &stats) {
    pause();
    auto j = http.post(std::string(prefix) + "nextSubject.jhtml", {{"courseId", options.course}});
    if (!j.contains("data") || !j["data"].is_object() || !j["data"].contains("nextSubject") ||
        !j["data"]["nextSubject"].is_object())
        throw std::runtime_error("题目结构异常，任务停止");
    auto &data = j["data"];
    auto &subject = data["nextSubject"];
    auto text = decrypt(subject.at("subDescript").get<std::string>());
    auto type = subject.at("subType").get<std::string>();
    // A platform verification question is a stop condition, never bypassed.
    if (guard_question(text))
        throw std::runtime_error("检测到平台验证题，请在官方应用中处理");
    if (type != "单选题" && type != "多选题" && type != "判断题") {
        std::cout << "不支持的题型，本次不计数" << std::endl;
        return Result::skipped;
    }
    auto ans = bank.by_text(text);
    if (stats.accuracy() > options.accuracy) {
        std::uniform_int_distribution<int> choice(0, type == "判断题" ? 1 : 3);
        ans = std::string(1, static_cast<char>('A' + choice(rng())));
    }
    if (ans.empty()) {
        std::cout << "题库未匹配，本次不提交" << std::endl;
        return Result::skipped;
    }
    if (ans.find_first_not_of("ABCD") != std::string::npos)
        throw std::runtime_error("题库答案格式异常");
    auto result =
        http.post(std::string(prefix) + "changeSituation.jhtml", {{"answer", ans},
                                                                  {"courseId", options.course},
                                                                  {"uuid", scalar(data.at("uuid"))},
                                                                  {"deviceUuid", ""}});
    auto classified = classify(result);
    if (classified == Result::skipped)
        throw std::runtime_error("平台未确认答题结果，停止以避免重复提交");
    return classified;
}
void Client::combat(Bank &bank, int matches) {
    for (int match = 0; match < matches; ++match) {
        auto start = http.post(std::string(prefix) + "startAnswerByManMachine.jhtml",
                               {{"courseId", options.course}});
        if (!start.value("isSuccess", false))
            throw std::runtime_error("无法开始AI对战");
        auto room = scalar(start.at("data").at("roomId"));
        // room identifiers are expected to be URL-safe tokens, never credentials.
        if (room.empty() ||
            room.find_first_not_of(
                "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-") !=
                std::string::npos)
            throw std::runtime_error("对战房间ID格式异常");
        Json subject = extract_subject(
            http.get(std::string(prefix) + "toAnswer.jhtml?roomId=" + room + "&fightType=1"));
        for (int round = 0; round < options.rounds; ++round) {
            auto text = decrypt(subject.at("subDescript").get<std::string>());
            if (guard_question(text))
                throw std::runtime_error("检测到平台验证题，停止对战");
            auto ans = answer_for(bank, subject);
            bool unknown = ans.empty();
            if (unknown)
                ans = "A";
            pause();
            auto reply = http.post(std::string(prefix) + "answerByManMachine.jhtml",
                                   {{"answer", ans}, {"roomId", room}});
            if (!reply.contains("data") || !reply["data"].is_object())
                throw std::runtime_error("对战返回结构异常");
            auto &data = reply["data"];
            if (unknown && data.contains("subjectCorrect")) {
                if (bank.learn(subject, scalar(data["subjectCorrect"])))
                    std::cout << "已新增题库记录" << std::endl;
            }
            std::cout << "对战 " << match + 1 << "/" << matches << "，回合 " << round + 1
                      << std::endl;
            if (!data.contains("subject") || data["subject"].is_null() || data["subject"].empty())
                break;
            subject = data["subject"];
        }
        auto final =
            http.post(std::string(prefix) + "findFightResultByRoomId.jhtml", {{"roomId", room}});
        auto &list = final.at("data").at("list");
        if (!list.is_array())
            throw std::runtime_error("对战结果格式异常");
        for (auto &item : list)
            if (item.value("studentName", std::string{}) != "AI")
                std::cout << "对战结果：" << scalar(item.at("fightResult")) << "；积分："
                          << scalar(item.at("getIntegral")) << std::endl;
    }
}
} // namespace qmyz
