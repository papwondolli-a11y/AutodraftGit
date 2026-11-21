"""AutoDraft 가 띄운 Blender 에 bpy 코드를 보내 실행한다 (Claude 연결 통로 클라이언트).

AutoDraft 3D 보기 창의 [Blender로 열기] 로 띄운 Blender 는 127.0.0.1 에서 명령을 받는다.
접속 정보(포트·토큰)는 %TEMP%/AutoDraft3D/blender_bridge.json 에 있다.

사용:
    python blender_bridge.py exec "print(len(bpy.data.objects))"
    python blender_bridge.py file  my_script.py
    python blender_bridge.py ping
"""

import json
import os
import socket
import sys
import tempfile

INFO_PATH = os.path.join(tempfile.gettempdir(), "AutoDraft3D", "blender_bridge.json")


def _info():
    try:
        with open(INFO_PATH, encoding="utf-8") as f:
            return json.load(f)
    except OSError:
        raise ConnectionError(
            "Blender 연결 정보가 없습니다. AutoDraft 3D 보기 창의 [Blender로 열기] 로 Blender 를 띄우세요.")


def run(code, timeout=120.0):
    """Blender 안에서 code 를 실행하고 {"ok", "output", "error"} 를 돌려준다."""
    info = _info()
    try:
        sock = socket.create_connection(("127.0.0.1", int(info["port"])), timeout=timeout)
    except OSError as exc:
        raise ConnectionError(
            "Blender 에 연결할 수 없습니다(포트 %s). Blender 가 닫혔다면 [Blender로 열기] 로 다시 띄우세요. (%s)"
            % (info.get("port"), exc))
    with sock:
        payload = json.dumps({"token": info["token"], "code": code}, ensure_ascii=False) + "\n"
        sock.sendall(payload.encode("utf-8"))
        buf = b""
        while b"\n" not in buf:
            chunk = sock.recv(1 << 16)
            if not chunk:
                break
            buf += chunk
    line = buf.split(b"\n", 1)[0]
    if not line:
        raise ConnectionError("Blender 가 응답하지 않았습니다.")
    return json.loads(line.decode("utf-8"))


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "ping":
        code = "print('blender', bpy.app.version_string, 'objects', len(bpy.data.objects))"
    elif cmd == "exec" and len(sys.argv) >= 3:
        code = sys.argv[2]
    elif cmd == "file" and len(sys.argv) >= 3:
        with open(sys.argv[2], encoding="utf-8") as f:
            code = f.read()
    else:
        print(__doc__)
        sys.exit(1)
    try:
        r = run(code)
    except ConnectionError as exc:
        print("ERROR:", exc)
        sys.exit(1)
    if r.get("output"):
        print(r["output"], end="" if r["output"].endswith("\n") else "\n")
    if not r.get("ok"):
        print("ERROR:", r.get("error"))
        sys.exit(2)


if __name__ == "__main__":
    main()
