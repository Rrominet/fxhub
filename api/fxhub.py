import json
import socket
import threading

# --- TcpClient replacement ---
class TcpClient:
    def __init__(self, url):
        host, port = url.split(":")
        self.host = host
        self.port = int(port)
        self.sock = None
        self.lock = threading.Lock()

    def connect(self):
        if self.sock is None:
            self.sock = socket.create_connection((self.host, self.port))

    def send_as_http(self, data, path):
        self.connect()
        body = json.dumps(data)
        req = (
            f"POST {path} HTTP/1.1\r\n"
            f"Host: {self.host}\r\n"
            f"Content-Type: application/json\r\n"
            f"Content-Length: {len(body)}\r\n\r\n"
            f"{body}"
        )
        with self.lock:
            self.sock.sendall(req.encode("utf-8"))
            res = self._recv_http()
        return res

    def _recv_http(self):
        chunks = []
        while True:
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk.decode("utf-8"))
            txt = "".join(chunks)
            if "\r\n\r\n" in txt or "\n\n" in txt:
                break
        return "".join(chunks)

    def listen_as_http(self, callback, path="/sse"):
        self.connect()
        req = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {self.host}\r\n"
            f"Accept: text/event-stream\r\n\r\n"
        )
        with self.lock:
            self.sock.sendall(req.encode("utf-8"))

        buf = ""
        while True:
            chunk = self.sock.recv(4096).decode("utf-8")
            if not chunk:
                break
            buf += chunk
            lines = buf.splitlines(True)
            complete = [l for l in lines if l.endswith("\n")]
            buf = "".join(l for l in lines if not l.endswith("\n"))
            for line in complete:
                callback(line.strip())


# --- fxhub globals ---
_url = "localhost:10001"
_client = TcpClient(_url)
_client_mtx = threading.Lock()
_listeners = {}
_listeners_mtx = threading.Lock()
_thread_pool = []


def _run_async(f):
    t = threading.Thread(target=f, daemon=True)
    _thread_pool.append(t)
    t.start()


def send_http(cmd, data):
    res_obj = {"sended": False}
    with _client_mtx:
        raw = _client.send_as_http(data, "/" + cmd)
    res_obj["sended"] = True
    try:
        parts = raw.split("\r\n\r\n")
        if len(parts) != 2:
            parts = raw.split("\n\n")
        if len(parts) != 2:
            res_obj["success"] = False
            res_obj["error"] = "Error in the response HTTP format."
            return res_obj
        _resdata = json.loads(parts[1])
    except Exception:
        res_obj["response"] = raw
        res_obj["success"] = False
        res_obj["error"] = "Couldn't parse the response as a JSON"
        return res_obj

    res_obj.update(_resdata)
    return res_obj


def send_event(app_id, event_type, async_=True, callback=None):
    def f():
        data = {"app-id": app_id, "type": event_type}
        res = send_http("send", data)
        if callback:
            callback(res)
        return res

    if async_:
        _run_async(f)
        return None
    return f()


def set_state(app_id, state, async_=True, callback=None):
    def f():
        data = {"app-id": app_id, "state": state}
        res = send_http("set-state", data)
        if callback:
            callback(res)
        return res

    if async_:
        _run_async(f)
        return None
    return f()


def get_state(app_id, async_=False, callback=None):
    def f():
        data = {"app-id": app_id}
        res = send_http("state", data)
        if callback:
            callback(res)
        return res

    if async_:
        _run_async(f)
        return None
    return f()


def add_listener(app_id, event_type, callback):
    with _listeners_mtx:
        _listeners[f"{app_id}_{event_type}"] = callback


def listen():
    def online(line):
        if not line.startswith("data:"):
            return
        try:
            payload = json.loads(line[5:])
        except Exception as e:
            print("Error parsing JSON from SSE:", e, line)
            return
        if "app-id" not in payload or "type" not in payload:
            return
        key = f"{payload['app-id']}_{payload['type']}"
        with _listeners_mtx:
            cb = _listeners.get(key)
        if cb:
            cb(payload)

    _client.listen_as_http(online, "/sse")


def listen_async():
    _run_async(listen)
