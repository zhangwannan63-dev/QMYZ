#include "qmyz/core.hpp"
#include <algorithm>
#include <chrono>
#include <curl/curl.h>
#include <stdexcept>
namespace qmyz {
namespace {
struct CurlInit {
    CurlInit() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
            throw std::runtime_error("curl初始化失败");
    }
    ~CurlInit() { curl_global_cleanup(); }
};
std::size_t body_cb(char *p, std::size_t a, std::size_t b, void *ctx) {
    auto &s = *static_cast<std::string *>(ctx);
    auto n = a * b;
    if (s.size() + n > 8 * 1024 * 1024)
        return 0;
    s.append(p, n);
    return n;
}
std::size_t header_cb(char *p, std::size_t a, std::size_t b, void *ctx) {
    std::string s(p, a * b), lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower.rfind("location:", 0) == 0) {
        auto value = s.substr(9);
        auto first = value.find_first_not_of(" \t"), last = value.find_last_not_of("\r\n \t");
        *static_cast<std::string *>(ctx) =
            first == std::string::npos ? "" : value.substr(first, last - first + 1);
    }
    return a * b;
}
} // namespace
Http::Http(const std::string &url) : origin(origin_of(url)) {
    static CurlInit init;
    handle = curl_easy_init();
    if (!handle)
        throw std::runtime_error("HTTP初始化失败");
    curl_easy_setopt(static_cast<CURL *>(handle), CURLOPT_COOKIEFILE, "");
}
Http::~Http() {
    if (handle)
        curl_easy_cleanup(static_cast<CURL *>(handle));
}
HttpResponse Http::request(const std::string &url,
                           const std::vector<std::pair<std::string, std::string>> *fields) {
    if (origin_of(url) != origin)
        throw std::runtime_error("拒绝向不同来源发送登录会话");
    auto *c = static_cast<CURL *>(handle);
    HttpResponse response;
    std::string form;
    auto escape = [&](const std::string &s) {
        char *raw = curl_easy_escape(c, s.c_str(), s.size());
        if (!raw)
            throw std::runtime_error("表单编码失败");
        std::string result(raw);
        curl_free(raw);
        return result;
    };
    if (fields)
        for (auto &f : *fields) {
            if (!form.empty())
                form += '&';
            form += escape(f.first) + "=" + escape(f.second);
        }
    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json,text/html");
    headers = curl_slist_append(headers, "X-Requested-With: XMLHttpRequest");
    headers = curl_slist_append(headers, ("Origin: " + origin).c_str());
    if (fields)
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> guard(headers, curl_slist_free_all);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "QMYZ-CPP/1.0");
    curl_easy_setopt(c, CURLOPT_REFERER, (origin + "/yiban-web/stu/homePage.jhtml").c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, body_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &response.location);
    if (fields) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, form.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(form.size()));
    } else {
        curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    }
    auto code = curl_easy_perform(c);
    // Never retry a POST: the platform may have accepted it before a timeout.
    if (code != CURLE_OK)
        throw std::runtime_error("HTTP传输失败（未自动重试，避免重复提交）");
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}
void Http::login(const std::string &url) {
    std::string current = url;
    for (int i = 0; i < 6; ++i) {
        auto r = request(current, nullptr);
        if (r.status >= 300 && r.status < 400) {
            if (r.location.empty())
                throw std::runtime_error("登录重定向缺少地址");
            if (r.location.rfind("//", 0) == 0)
                throw std::runtime_error("拒绝跨来源重定向");
            if (r.location[0] == '/')
                current = origin + r.location;
            else if (r.location.find("://") != std::string::npos)
                current = r.location;
            else
                current = current.substr(0, current.find_last_of('/') + 1) + r.location;
            if (origin_of(current) != origin)
                throw std::runtime_error("登录需要跨来源跳转，请提供最终平台链接");
            continue;
        }
        if (r.status != 200)
            throw std::runtime_error("登录HTTP状态异常");
        curl_slist *cookies = nullptr;
        curl_easy_getinfo(static_cast<CURL *>(handle), CURLINFO_COOKIELIST, &cookies);
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> guard(cookies,
                                                                          curl_slist_free_all);
        bool found = false;
        for (auto *p = cookies; p; p = p->next) {
            std::string s = p->data;
            if (s.find("\tJSESSIONID\t") != std::string::npos)
                found = true;
        }
        if (!found)
            throw std::runtime_error("未获取登录会话，请更新链接");
        return;
    }
    throw std::runtime_error("登录重定向次数过多");
}
std::string Http::get(const std::string &path) {
    auto r = request(origin + path, nullptr);
    if (r.status != 200)
        throw std::runtime_error("GET状态异常，可能需要重新登录");
    return r.body;
}
Json Http::post(const std::string &path,
                const std::vector<std::pair<std::string, std::string>> &fields) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    auto r = request(origin + path + (path.find('?') == std::string::npos ? "?" : "&") +
                         "_=" + std::to_string(now),
                     &fields);
    if (r.status != 200)
        throw std::runtime_error("POST状态异常，未计为提交");
    auto j = Json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        throw std::runtime_error("平台未返回JSON，可能登录已过期");
    return j;
}
} // namespace qmyz
