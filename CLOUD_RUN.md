# C++云端运行

1. 本分支为 `cpp-rewrite`。仓库默认分支仍保留Python版本。在 Actions → QMYZ C++ 云端运行 中选择 `cpp-rewrite`；若默认分支的页面未列出新名称，打开 qmyz.yml 工作流并在 Run workflow 的分支选项切换为 `cpp-rewrite`。
2. 登录链接存入 Settings → Secrets and variables → Actions，对应 `QMYZ_LOGIN_URL` 或 `_2` 至 `_7` 槽位。已有Secrets可复用；不要把登录URL放到输入框、代码或Issue中。
3. mode选择 `answer`，课程7为毛概、9为马原；count填写实际提交题数；accuracy填写目标正确率。
4. 启动后展开“Run C++ application”，逐题查看真实提交进度。达到目标才显示任务完成。未知题/失败不计；网络、登录和验证异常可能提前失败。
5. 每个账号单独提交一次。不同槽位可并行，GitHub配额仍可能使其排队。同一槽位不并行，待运行任务只保留最新一个，不要重复点击同一账号。

运行最长120分钟。构建安装系统依赖，不需要Python生产运行环境。answer任务保留不含凭据的summary.json；fight任务保存更新后的题库CSV供下载。课程列表及离线检查也可通过mode选择。

仓库工作流权限仅contents:read，不自动提交CSV，不改变账号的其他仓库设置。请只使用获得授权的账号。
