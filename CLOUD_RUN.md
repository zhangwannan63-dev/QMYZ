# 云端运行

1. 打开本仓库 Settings → Secrets and variables → Actions → New repository secret。
2. 名称填写 `QMYZ_LOGIN_URL`；Secret 填写从易班青马易战首页复制的完整登录地址。不要提交到代码、Issue 或运行参数里。
3. 打开 Actions → QMYZ 云端运行 → Run workflow。
4. 选择课程 ID，填写答题数量（1–500，默认 10），点击 Run workflow。
5. 打开运行记录查看结果。最长运行 60 分钟；同一仓库任务不并行运行。

登录地址获取方式：打开易班青马易战，进入含视频的首页，点击右上角复制地址。地址失效时更新同名 Secret。

工作流使用 Python 3.11，安装 requirements.txt 后执行 qm_cli.py，准确度参数为 100。代码不再打印登录 Cookie；异常只显示错误类型。题库、接口和登录状态仍可能导致任务失败。当前配置不包含定时运行。
