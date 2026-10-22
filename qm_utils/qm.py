import requests
import csv
import time
import qm_utils.qm_tools as qm_tools
import random
import re
from urllib.parse import urlsplit


class qm_tiku:
    def __init__(self, csv_file_path):
        self.tiku = self.loadCSV(csv_file_path)
        self.num = [0, 0]

    def loadCSV(self, csv_file_path):
        with open(csv_file_path, "r", encoding="utf-8") as f:
            reader = csv.reader(f)
            tiku = list(reader)
        return tiku


class qm_auto:
    def __init__(self, url, tiku, courseId, JSESSIONID="", accuracy=100):
        self.url = url
        parsed = urlsplit(url)
        if parsed.scheme not in ("http", "https") or not parsed.netloc:
            raise ValueError("登录链接格式错误")
        self.base_url = parsed.scheme + "://" + parsed.netloc
        self.tiku = tiku
        self.JSESSIONID = JSESSIONID
        self.key_base64 = "ZDBmMTNiZGI3MDRhMWVhMWE3MTcwNjJiNTk0NzY0ODg"
        self.courseId = courseId
        self.accuracy = accuracy
        self.headers = {
            "Accept": "application/json",
            "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6",
            "Cache-Control": "no-cache",
            "Connection": "keep-alive",
            "Content-Type": "application/x-www-form-urlencoded",
            "Origin": self.base_url,
            "Pragma": "no-cache",
            "Referer": self.base_url + "/yiban-web/stu/toSubject.jhtml?courseId="
            + self.courseId,
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36 Edg/128.0.0.0",
            "X-Requested-With": "XMLHttpRequest",
        }
        self.cookies = {
            "JSESSIONID": (
                self.JSESSIONID if self.JSESSIONID != "" else self.get_cookie_from_url()
            ),
        }
        self.params = {
            "_": str(time.time_ns())[:13],
        }
        self.data = {
            "courseId": self.courseId,
        }

        self.AB = {0: "A", 1: "B", 2: "C", 3: "D"}
        self.num = [0, 0]

    def calculate_accuracy(self):
        if self.num[0] + self.num[1] == 0:
            return 0
        else:
            return self.num[0] / (self.num[0] + self.num[1]) * 100

    def judge_request(self, res):
        if res.status_code != 200:
            print("请求失败，状态码为：" + str(res.status_code))
            return False
        else:
            return True

    def get_cookie_from_url(self):
        session = requests.Session()
        res = session.get(self.url, timeout=30)
        res.raise_for_status()
        cookie = next((item.value for item in session.cookies if item.name == "JSESSIONID"), None)
        if not cookie:
            raise ValueError("未获取到登录会话，请更新登录链接")
        print("登录会话已获取")
        self.JSESSIONID = cookie
        return cookie

    def do(self):
        ti = requests.post(
            self.base_url + "/yiban-web/stu/nextSubject.jhtml",
            params=self.params,
            cookies=self.cookies,
            headers=self.headers,
            data=self.data,
            timeout=30,
        )
        if not self.judge_request(ti):
            return False
        sub_descript = qm_tools.aes_ecb_decrypt(
            ti.json()["data"]["nextSubject"]["subDescript"], self.key_base64
        )
        type = ti.json()["data"]["nextSubject"]["subType"]
        uuid = ti.json()["data"]["uuid"]
        if type not in ["单选题", "判断题", "多选题"]:
            print(f"暂不支持该类型: {type}")
            return False
        elif "刷题" in sub_descript or "请选择" in sub_descript:  # 防刷题题目检测
            print("检测到防刷题题目, 自动跳过")
            return False
        flag = False
        for t in self.tiku:
            if sub_descript == t[4]:
                print(f"查询到题目: {sub_descript}")
                flag = True
                if self.calculate_accuracy() > self.accuracy:
                    print(f"超过准确度要求{self.accuracy}%")
                    right_ans = (
                        self.AB[random.randint(0, 1)]
                        if type == "判断题"
                        else self.AB[random.randint(0, 3)]
                    )
                else:
                    right_ans = t[-1]
                break
        if not flag:
            print(f"未查询到题目: {sub_descript}")
            if self.calculate_accuracy() > self.accuracy:
                print(f"超过准确度要求{self.accuracy}%")
                right_ans = (
                    self.AB[random.randint(0, 1)]
                    if type == "判断题"
                    else self.AB[random.randint(0, 3)]
                )
            else:
                return False
        ans_data = {
            "answer": right_ans,
            "courseId": self.courseId,
            "uuid": uuid,
            "deviceUuid": "",
        }
        ans_res = requests.post(
            self.base_url + "/yiban-web/stu/changeSituation.jhtml",
            params=self.params,
            cookies=self.cookies,
            headers=self.headers,
            data=ans_data,
            timeout=30,
        )
        if not self.judge_request(ans_res):
            return False
        result = ans_res.json()
        message = str(result.get("message", ""))
        if message == "回答正确！":
            self.num[0] += 1
            print(f"答题正确, 当前答对{self.num[0]}道，准确率为{self.calculate_accuracy()}")
        elif "回答错误" in message or "回答不正确" in message:
            self.num[1] += 1
            print(f"答题错误, 当前答错{self.num[1]}道，准确率为{self.calculate_accuracy()}")
        else:
            print("平台未返回明确答题结果，本次不计数")
            return False
        return True

    def auto_do(self, num):
        if not isinstance(num, int) or num <= 0:
            raise ValueError("必须指定正整数的实际提交题数")
        initial = sum(self.num)
        attempts = 0
        consecutive_skips = 0
        while sum(self.num) - initial < num:
            time.sleep(random.randint(3, 6))
            attempts += 1
            before = sum(self.num)
            self.do()
            completed = sum(self.num) - initial
            if sum(self.num) > before:
                consecutive_skips = 0
            else:
                consecutive_skips += 1
            print(f"实际提交进度：{completed}/{num}；尝试次数：{attempts}", flush=True)
            if consecutive_skips >= 100:
                raise RuntimeError(f"连续100次未收到明确答题结果，未完成目标：{completed}/{num}")
        print(f"任务完成：实际提交{num}题；答对{self.num[0]}，答错{self.num[1]}；正确率{self.calculate_accuracy():.4f}%", flush=True)
