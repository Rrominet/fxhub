const net = require("net");
const { StringDecoder } = require("string_decoder");

// --- TcpClient replacement ---
class TcpClient {
    constructor(url) {
        const [host, port] = url.split(":");
        this.host = host;
        this.port = parseInt(port, 10);
        this.socket = null;
        this.lock = Promise.resolve(); // crude lock
    }

    async connect() {
        if (this.socket) return;
        this.socket = new net.Socket();
        await new Promise((resolve, reject) => {
            this.socket.connect(this.port, this.host, resolve);
            this.socket.on("error", reject);
        });
    }

    async sendAsHttp(data, path) {
        await this.connect();
        const body = JSON.stringify(data);
        const req =
            `POST ${path} HTTP/1.1\r\n` +
            `Host: ${this.host}\r\n` +
            `Content-Type: application/json\r\n` +
            `Content-Length: ${Buffer.byteLength(body)}\r\n\r\n` +
            body;

        // lock to avoid simultaneous writes
        this.lock = this.lock.then(
            () =>
                new Promise((resolve, reject) => {
                    let res = "";
                    const onData = chunk => {
                        res += chunk.toString("utf-8");
                        // crude: break once we saw end of headers + body
                        if (res.includes("\r\n\r\n") || res.includes("\n\n")) {
                            this.socket.removeListener("data", onData);
                            resolve(res);
                        }
                    };
                    this.socket.on("data", onData);
                    this.socket.write(req);
                })
        );
        return this.lock;
    }

    async listenAsHttp(callback, path = "/sse") {
        await this.connect();
        const req =
            `GET ${path} HTTP/1.1\r\n` +
            `Host: ${this.host}\r\n` +
            `Accept: text/event-stream\r\n\r\n`;

        this.socket.write(req);

        let decoder = new StringDecoder("utf-8");
        let buf = "";
        this.socket.on("data", chunk => {
            buf += decoder.write(chunk);
            let lines = buf.split(/\r?\n/);
            buf = lines.pop(); // keep partial
            for (const line of lines) {
                callback(line.trim());
            }
        });
    }
}

// --- fxhub globals ---
const _url = "localhost:10001";
const _client = new TcpClient(_url);
const _listeners = {};
let _clientLock = false;

function _runAsync(f) {
    setImmediate(f);
}

async function send_http(cmd, data) {
    let resObj = { sended: false };
    let raw = await _client.sendAsHttp(data, "/" + cmd);
    resObj.sended = true;
    try {
        let parts = raw.split("\r\n\r\n");
        if (parts.length !== 2) parts = raw.split("\n\n");
        if (parts.length !== 2) {
            resObj.success = false;
            resObj.error = "Error in the response HTTP format.";
            return resObj;
        }
        let resdata = JSON.parse(parts[1]);
        Object.assign(resObj, resdata);
    } catch (e) {
        resObj.response = raw;
        resObj.success = false;
        resObj.error = "Couldn't parse the response as a JSON";
    }
    return resObj;
}

function send_event(app_id, event_type, async_ = true, callback = null) {
    const f = async () => {
        const data = { "app-id": app_id, type: event_type };
        const res = await send_http("send", data);
        if (callback) callback(res);
        return res;
    };
    if (async_) {
        _runAsync(f);
        return null;
    } else {
        return f();
    }
}

function set_state(app_id, state, async_ = true, callback = null) {
    const f = async () => {
        const data = { "app-id": app_id, state: state };
        const res = await send_http("set-state", data);
        if (callback) callback(res);
        return res;
    };
    if (async_) {
        _runAsync(f);
        return null;
    } else {
        return f();
    }
}

function get_state(app_id, async_ = false, callback = null) {
    const f = async () => {
        const data = { "app-id": app_id };
        const res = await send_http("state", data);
        if (callback) callback(res);
        return res;
    };
    if (async_) {
        _runAsync(f);
        return null;
    } else {
        return f();
    }
}

function addListener(app_id, event_type, callback) {
    _listeners[`${app_id}_${event_type}`] = callback;
}

function listen() {
    function online(line) {
        if (!line.startsWith("data:")) return;
        let payload;
        try {
            payload = JSON.parse(line.slice(5));
        } catch (e) {
            console.error("Error parsing JSON from SSE:", e, line);
            return;
        }
        if (!("app-id" in payload) || !("type" in payload)) return;
        const key = `${payload["app-id"]}_${payload["type"]}`;
        const cb = _listeners[key];
        if (cb) cb(payload);
    }
    _client.listenAsHttp(online, "/sse");
}

function listen_async() {
    _runAsync(listen);
}

module.exports = {
    send_http,
    send_event,
    set_state,
    get_state,
    addListener,
    listen,
    listen_async
};
