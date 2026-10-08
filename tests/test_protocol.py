#!/usr/bin/env python3
"""Protocol tests: starts build/peppy-mock on a spare port and checks the
server, MCP, batching, batch references, quiet replies, undo steps and errors."""

import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 7390
home = tempfile.mkdtemp(prefix="peppy-test-")
env = dict(os.environ, PEPPY_PORT=str(PORT), PEPPY_HOME=home)
proc = subprocess.Popen([os.path.join(ROOT, "build", "peppy-mock")], env=env, stdout=subprocess.PIPE, text=True)
proc.stdout.readline()
session = json.load(open(os.path.join(home, "session.json")))
TOKEN = session["token"]
failures = 0


def post(path, body, headers=None):
    req = urllib.request.Request(f"http://127.0.0.1:{session['port']}{path}", data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json", "User-Agent": "claude-code/test", **(headers or {})})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, json.loads(r.read() or b"null")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"null")


def rpc(method, params=None):
    return post(f"/rpc/{TOKEN}", {"jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}})[1]


def mcp(method, params=None, id=1):
    return post(f"/mcp/{TOKEN}", {"jsonrpc": "2.0", "id": id, "method": method, "params": params or {}})[1]


def tool(name, args):
    r = mcp("tools/call", {"name": name, "arguments": args})["result"]
    text = r["content"][0]["text"]
    try:
        return r, json.loads(text)
    except json.JSONDecodeError:
        return r, text


def check(name, cond, detail=""):
    global failures
    print(("  ok   " if cond else "  FAIL ") + name + ("" if cond else f"   {detail}"))
    failures += 0 if cond else 1


try:
    print("server")
    code, body = post("/rpc/wrong", {"method": "app.info"})
    check("refuses a wrong token", code == 401 and "Copy connection" in body["error"]["message"])
    code, _ = post(f"/rpc/{TOKEN}", {"method": "app.info"}, {"Origin": "https://evil.example"})
    check("refuses browsers", code == 403)
    code, _ = post(f"/mcp", {"jsonrpc": "2.0", "id": 1, "method": "ping"}, {"Authorization": f"Bearer {TOKEN}"})
    check("takes the token as a bearer header", code == 200)
    check("session file is private", oct(os.stat(os.path.join(home, "session.json")).st_mode & 0o777) == "0o600")

    print("mcp")
    init = mcp("initialize", {"protocolVersion": "2025-06-18", "clientInfo": {"name": "claude-code"}})["result"]
    check("initialize", init["serverInfo"]["name"] == "peppy-indesign" and "page coordinates" in init["instructions"])
    code, _ = post(f"/mcp/{TOKEN}", {"jsonrpc": "2.0", "method": "notifications/initialized"})
    check("notification gets 202", code == 202)
    tools = [t["name"] for t in mcp("tools/list")["result"]["tools"]]
    check("everyday tools + peppy_*", {"peppy_find", "peppy_call", "peppy_batch", "item_transform", "frame_text"} <= set(tools), tools)
    check("small surface (< 30 tools)", len(tools) < 30, len(tools))
    all_tools = post(f"/mcp/{TOKEN}?tools=all", {"jsonrpc": "2.0", "id": 1, "method": "tools/list"})[1]["result"]["tools"]
    check("?tools=all lists every command", len(all_tools) > 60, len(all_tools))
    r, found = tool("peppy_find", {"search": "paragraph style"})
    check("peppy_find", any(f["method"] == "style.apply" for f in found), found[:3])

    print("commands")
    r, info = tool("peppy_call", {"method": "document.info"})
    check("no document -> honest error with hint", r.get("isError") and "Hint:" in info, info)
    tool("peppy_call", {"method": "document.new", "params": {"pages": 2}})
    r, frame = tool("frame_text", {"x": 36, "y": 36, "width": 200, "height": 100, "contents": "Hello", "name": "Headline"})
    check("frame_text makes a frame", frame["type"] == "TextFrame" and frame["bounds"]["x"] == 36, frame)
    r, moved = tool("item_transform", {"ids": [frame["id"]], "translate": [10, 5]})
    check("item_transform moves it", moved[0]["bounds"]["x"] == 46 and moved[0]["bounds"]["y"] == 41, moved)
    r, err = tool("item_get", {"id": 99999})
    check("unknown id -> not found", r.get("isError") and "no item" in err, err)
    r, err = tool("peppy_call", {"method": "nope.nothing"})
    check("unknown method -> hint to peppy_find", r.get("isError") and "peppy_find" in err, err)
    r, err = tool("peppy_call", {"method": "table.create", "params": {"rows": 2, "columns": 2}})
    check("unbuilt command says so", r.get("isError") and "isn't built yet" in err, err)

    print("batch")
    r, steps = tool("peppy_batch", {"calls": [
        {"method": "frame.rect", "params": {"x": 0, "y": 0, "width": 50, "height": 50, "page": "2"}},
        {"method": "item.duplicate", "params": {"ids": ["$0.id"], "translate": [60, 0]}},
        {"method": "item.set", "params": {"ids": ["$1.id"], "fill": "Black"}},
        {"method": "item.bounds", "params": {"id": "$1.id", "y": "$0.bounds.height"}},
    ]})
    check("batch references resolve", not r.get("isError") and steps[3]["bounds"]["y"] == 50 and steps[3]["bounds"]["x"] == 60, steps)
    tree = tool("item_tree", {"page": "2"})[1]
    check("both rects on page 2", len(tree) == 2, tree)
    r, undone = tool("history_undo", {})
    tree = tool("item_tree", {"page": "2"})[1]
    check("one undo removes the whole batch", len(tree) == 0 and undone["undone"] == ["Peppy: 4 changes"], (tree, undone))

    r, steps = tool("peppy_batch", {"calls": [
        {"method": "frame.rect", "params": {"x": 0, "y": 0, "width": 10, "height": 10}},
        {"method": "item.get", "params": {"id": 424242}},
        {"method": "item.delete", "params": {"ids": ["$0.id"]}},
    ]})
    check("batch stops at the first error", r.get("isError") and "error" in steps[1] and "skipped" in json.dumps(steps[2]), steps)
    r, steps = tool("peppy_batch", {"calls": [{"method": "item.get", "params": {"id": "$5.id"}}]})
    check("forward reference refused", r.get("isError") and "earlier step" in json.dumps(steps), steps)

    r, q = tool("peppy_call", {"method": "item.get", "params": {"id": frame["id"]}, "quiet": True})
    check("quiet trims the reply", set(q) <= {"id", "name", "type", "bounds"} and "contents" not in q, q)
    r, steps = tool("peppy_batch", {"quiet": True, "calls": [
        {"method": "item.get", "params": {"id": frame["id"]}},
        {"method": "text.get", "params": {"id": "$0.id"}}]})
    check("quiet batch still references full results", not r.get("isError") and steps[1] == {"id": frame["id"]}, steps)

    print("rpc")
    out = post(f"/rpc/{TOKEN}", [{"jsonrpc": "2.0", "id": 1, "method": "app.info"}, {"jsonrpc": "2.0", "id": 2, "method": "page.list"}])[1]
    check("plain JSON-RPC batch", isinstance(out, list) and out[1]["result"][1]["name"] == "2", out)
finally:
    proc.terminate()

print(f"\n{'all passed' if not failures else f'{failures} failed'}")
sys.exit(1 if failures else 0)
