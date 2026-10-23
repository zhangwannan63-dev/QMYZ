"""Offline-only protocol tests. No real platform credentials or requests."""
import base64
import csv
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from urllib.parse import parse_qs, urlsplit

EXE = str(Path(sys.argv[1]).resolve())
KEY = b"d0f13bdb704a1ea1a717062b59476488".hex()
def encrypted(text):
    raw = subprocess.run(["openssl", "enc", "-aes-256-ecb", "-K", KEY], input=text.encode(), capture_output=True, check=True).stdout
    return base64.b64encode(raw).decode().rstrip("=")

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def reply(self, value, cookie=False, status=200):
        raw = (json.dumps(value, ensure_ascii=False) if isinstance(value, dict) else value).encode()
        self.send_response(status)
        if cookie: self.send_header("Set-Cookie", "JSESSIONID=fake-test-cookie; Path=/; HttpOnly")
        self.end_headers()
        self.wfile.write(raw)
    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/login": return self.reply("ok", True)
        if "fake-test-cookie" not in self.headers.get("Cookie", ""): return self.reply("no", status=403)
        if path.endswith("toCourse.jhtml"):
            return self.reply('<a href="toSubject.jhtml?courseId=9"><div class="mui-media-body">马克思主义基本原理</div></a>')
        if path.endswith("toAnswer.jhtml"):
            return self.reply('<script>const unrelated={"x":1};const subject='+json.dumps(self.server.subject, ensure_ascii=False)+';</script>')
        self.reply("missing", status=404)
    def do_POST(self):
        path=urlsplit(self.path).path
        form=parse_qs(self.rfile.read(int(self.headers.get("Content-Length",0))).decode(), keep_blank_values=True)
        if "fake-test-cookie" not in self.headers.get("Cookie", ""): return self.reply("no", status=403)
        if path.endswith("nextSubject.jhtml"):
            self.server.fetches+=1
            text = "未知题" if self.server.fetches==1 else "已知题"
            if self.server.scenario=="guard": text="请选择验证选项"
            return self.reply({"data":{"nextSubject":{"subType":"单选题","subDescript":encrypted(text)},"uuid":"fake-id"}})
        if path.endswith("changeSituation.jhtml"):
            self.server.submissions+=1
            assert form["courseId"]==["9"] and form["answer"]==["A"]
            if self.server.scenario=="ambiguous": return self.reply({"message":"登录失效"})
            return self.reply({"isSuccess":True,"message":"回答错误！" if self.server.submissions==2 else "回答正确！"})
        if path.endswith("startAnswerByManMachine.jhtml"): return self.reply({"isSuccess":True,"data":{"roomId":"room1"}})
        if path.endswith("answerByManMachine.jhtml"):
            self.server.combat+=1
            return self.reply({"data":{"subjectCorrect":"A","subject":None if self.server.combat==6 else self.server.subject}})
        if path.endswith("findFightResultByRoomId.jhtml"): return self.reply({"data":{"list":[{"studentName":"AI","fightResult":"lose","getIntegral":0},{"studentName":"user","fightResult":"win","getIntegral":5}]}})
        self.reply("missing",status=404)

with tempfile.TemporaryDirectory() as temp:
    root=Path(temp); bank=root/"9.csv"
    with bank.open("w",newline="") as f:
        csv.writer(f).writerows([["courseId","id","subType","optionCount","subDescript","option0","option1","option2","option3","answer"],["9","1","单选题","2","已知题","甲","乙","","","A"]])
    server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
    server.fetches=server.submissions=server.combat=0;server.scenario="normal"
    server.subject={"courseId":9,"subjectId":42,"subType":"单选题","optionCount":2,"subDescript":encrypted("对战新题"),"option0":encrypted("甲"),"option1":encrypted("乙")}
    threading.Thread(target=server.serve_forever,daemon=True).start()
    env={**os.environ,"QMYZ_LOGIN_URL":f"http://127.0.0.1:{server.server_port}/login?verify_request=DO_NOT_LOG"}
    def run(mode,*extra,ok=True):
        result=subprocess.run([EXE,mode,"--delay-min","0","--delay-max","0",*map(str,extra)],env=env,capture_output=True,text=True,timeout=20)
        assert (result.returncode==0)==ok,(result.stdout,result.stderr)
        assert "DO_NOT_LOG" not in result.stdout+result.stderr and "fake-test-cookie" not in result.stdout+result.stderr
        return result
    out=run("answer","-i",9,"-c",bank,"-n",3,"--summary",root/"summary.json")
    stats=json.loads((root/"summary.json").read_text())
    assert stats["submitted"]==3 and stats["correct"]==2 and stats["wrong"]==1 and stats["skipped"]==1
    assert server.submissions==3 and server.fetches==4
    assert "马克思主义基本原理" in run("courses").stdout
    server.scenario="ambiguous";before=server.submissions
    run("answer","-i",9,"-c",bank,"-n",3,ok=False)
    assert server.submissions==before+1  # ambiguous POST is never retried
    server.scenario="guard";before=server.submissions
    run("answer","-i",9,"-c",bank,"-n",1,ok=False)
    assert server.submissions==before  # no bypass of platform verification
    server.scenario="normal"
    fightbank=root/"fight.csv"
    run("fight","-i",9,"-c",fightbank,"-n",1)
    assert server.combat==6
    with fightbank.open() as f: records=list(csv.reader(f))
    assert len(records)==2 and records[1][4]=="对战新题" and records[1][-1]=="A"
    run("check-bank","-c",fightbank)
    run("answer","-i",9,"-c",bank,"-n",0,ok=False)
    server.shutdown()
print("Offline HTTP integration passed: exact count, courses, combat, CSV learning, guarded failures, redaction")
