# QMYZ C++

青马易战项目的 C++17 重写版。源项目：[shibig666/QMYZ](https://github.com/shibig666/QMYZ)。沿用 GPLv3 许可证与原 CSV 题库，保留作者署名。

**功能代码全部使用 C++，不调用 Python 程序。** Python 仅用于可选的离线集成测试。HTTP 使用 libcurl，AES-ECB/PKCS7 使用 OpenSSL，JSON 使用 nlohmann/json。

## 编译

Ubuntu/Debian：

```sh
sudo apt-get update
sudo apt-get install -y g++ cmake libcurl4-openssl-dev libssl-dev nlohmann-json3-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

需要 C++17 编译器、CMake >= 3.18。macOS/Linux 可使用对应包管理器安装上述三个开发库，并通过 CMAKE_PREFIX_PATH 指定依赖目录。Windows 尚未验证。

## 功能对照

| 原入口/功能 | C++入口 | 说明 |
|---|---|---|
| qm_cli.py | `qmyz answer` 或 `qm_cli` | 兼容 `-u -i -c -n -a` |
| qm_auto.py | `qmyz interactive` 或 `qm_auto` | 登录、列课、题库存在标记、交互选课和数量 |
| fight.py | `qmyz fight` 或 `fight` | AI对战、默认6回合、读取结算与积分 |
| 对战补充题库 | 同上 | 解密题干及选项、追加平台返回的正确答案、按课程+题号去重 |
| get_courses | `qmyz courses` | 获取平台课程列表 |
| AES工具 | `decrypt()` | 支持缺失的Base64补位、严格PKCS7校验 |
| CSV读取 | `Bank` | UTF-8 BOM、逗号、双引号、换行、CRLF |
| 云端运行 | GitHub Actions | 7个账号槽位，不同账号可并行 |
| 新增离线检查 | `qmyz check-bank` | 无需登录检查CSV格式并显示有效题量 |

## 使用

优先使用环境变量，避免登录链接出现在命令行历史或进程参数中。不把链接提交到仓库。

```sh
export QMYZ_LOGIN_URL='从官方应用复制的完整登录链接'
./build/qmyz answer -i 9 -n 300 -a 100 --summary summary.json
./build/qmyz interactive
./build/qmyz courses
./build/qmyz fight -i 7 -c qmyz/7.csv -n 1
./build/qmyz check-bank -c qmyz/9.csv
./build/qmyz --help
```

`answer -n` 是**收到平台明确答题结果的实际提交数量**，对/错都算，未知题或不支持题型不算。到达目标才正常完成；若接口异常、会话失效、平台验证题、连续100次未提交，返回非零退出码并报告未完成。外部平台可用性与题库覆盖率影响能否完成，不能保证无限等待一定答够。

`-a` 保留原项目目标比例控制：目前累计正确率超过目标才从有效字母范围随机选答；100时始终采用题库答案。题库答案可能错误，因此**100是目标参数，不能保证最终100%**。每个新进程单独统计，补答不会自动合并之前任务的正确率。

`fight -n` 是对战场数，默认每场最多6次提交，`--rounds` 可设置1–100。未命中题库时沿用原程序的A选项，使用平台在答题后返回的 `subjectCorrect` 补库。服务端提前结束则停止该场。题库追加成功立即生效；云端更新后的CSV在 artifacts 下载，不自动写回仓库。

## 结构

```text
include/qmyz/core.hpp   公共类型、模块接口
src/core.cpp           CSV、AES、索引、计数、题目JSON提取
src/http.cpp           会话、同源限制、超时、表单编码
src/client.cpp         课程、答题、AI对战业务
src/main.cpp           CLI和交互入口
tests/                 C++单测、本地模拟平台集成测试
qmyz/                  原有题库（原样保留）
.github/workflows/     编译测试、云端执行
docs/PARITY.md         兼容范围及差异
```

题库查询使用哈希索引，避免每道题线性扫描整个CSV。HTTP会话复用连接，自动管理Cookie，不打印Cookie或登录URL；输出逐条刷新。请求有连接及总超时，启用TLS证书校验，拒绝跨来源携带会话，提交失败不自动重试以避免重复提交。

## 云端执行

见 [CLOUD_RUN.md](CLOUD_RUN.md)。本次重写在 `cpp-rewrite` 分支，`master` 上的 Python 版本继续保留。Actions 要使用含C++版本的分支运行，不能在Python分支使用C++命令。

## 验证

```sh
sudo apt-get install -y python3 openssl  # 仅离线集成测试需要
ctest --test-dir build --output-on-failure
for file in qmyz/*.csv; do ./build/qmyz check-bank -c "$file"; done
```

测试覆盖：AES中文与Base64补位/非法输入、复杂CSV与BOM、题库索引与去重、HTML题目提取、实际提交计数与跳过上限，以及本地HTTP模拟的答题、选课、6回合对战、补库、验证题停止、会话信息不泄露。测试不会访问真实平台。

这是依据现有 Python 源码与模拟协议完成的移植；尚未使用真实账号进行C++端到端提交验证，特别是对战页面结构和返回字段可能随平台更新变化。不能声称与线上所有行为完全一致。具体差异见 [PARITY.md](docs/PARITY.md)。
