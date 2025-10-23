use std::collections::HashMap;
use std::io::{Read, Write, BufRead, BufReader};
use std::net::TcpStream;
use std::sync::{Mutex, Arc};
use std::thread;
use serde_json::Value;

lazy_static::lazy_static! {
    static ref URL: String = "localhost:10001".to_string();
    static ref CLIENT: Mutex<TcpClient> = Mutex::new(TcpClient::new(&URL));
    static ref CLIENT_WRITE: Mutex<()> = Mutex::new(());
    static ref LISTENERS: Mutex<HashMap<String, Box<dyn Fn(Value) + Send + Sync>>> = Mutex::new(HashMap::new());
}

struct TcpClient {
    host: String,
    port: u16,
    conn: Option<TcpStream>,
}

impl TcpClient {
    fn new(url: &str) -> Self {
        let parts: Vec<&str> = url.split(':').collect();
        let host = parts[0].to_string();
        let port = parts[1].parse::<u16>().unwrap();
        TcpClient { host, port, conn: None }
    }

    fn connect(&mut self) {
        if self.conn.is_none() {
            self.conn = Some(TcpStream::connect((&*self.host, self.port)).unwrap());
        }
    }

    fn send_as_http(&mut self, data: &Value, path: &str) -> String {
        self.connect();
        let body = data.to_string();
        let req = format!(
            "POST {} HTTP/1.1\r\nHost: {}\r\nContent-Type: application/json\r\nContent-Length: {}\r\n\r\n{}",
            path, self.host, body.len(), body
        );
        let conn = self.conn.as_mut().unwrap();
        conn.write_all(req.as_bytes()).unwrap();
        let mut buf = String::new();
        conn.read_to_string(&mut buf).unwrap();
        buf
    }

    fn listen_as_http(&mut self, callback: Arc<dyn Fn(String) + Send + Sync>, path: &str) {
        self.connect();
        let req = format!(
            "GET {} HTTP/1.1\r\nHost: {}\r\nAccept: text/event-stream\r\n\r\n",
            path, self.host
        );
        let conn = self.conn.as_mut().unwrap();
        conn.write_all(req.as_bytes()).unwrap();

        let reader = BufReader::new(conn.try_clone().unwrap());
        for line in reader.lines() {
            if let Ok(l) = line {
                callback(l);
            }
        }
    }
}

fn send_http(cmd: &str, data: Value) -> Value {
    let mut res = serde_json::json!({"sended": false});
    let _guard = CLIENT_WRITE.lock().unwrap();
    let mut client = CLIENT.lock().unwrap();
    let raw = client.send_as_http(&data, &format!("/{}", cmd));
    res["sended"] = Value::Bool(true);

    let parts: Vec<&str> = raw.split("\r\n\r\n").collect();
    if parts.len() != 2 {
        return serde_json::json!({
            "success": false,
            "error": "Bad HTTP format"
        });
    }
    match serde_json::from_str(parts[1]) {
        Ok(parsed) => {
            let mut parsed_map: Value = parsed;
            if let Some(obj) = parsed_map.as_object_mut() {
                for (k, v) in obj.iter() {
                    res[k] = v.clone();
                }
            }
        }
        Err(_) => {
            res["success"] = Value::Bool(false);
            res["error"] = Value::String("Couldn't parse JSON".to_string());
        }
    }
    res
}

fn add_listener(app_id: &str, event_type: &str, callback: Box<dyn Fn(Value) + Send + Sync>) {
    let mut map = LISTENERS.lock().unwrap();
    map.insert(format!("{}_{}", app_id, event_type), callback);
}

fn listen() {
    let callback = Arc::new(|line: String| {
        if !line.starts_with("data:") {
            return;
        }
        if let Ok(payload) = serde_json::from_str::<Value>(&line[5..]) {
            let id = format!(
                "{}_{}",
                payload["app-id"].as_str().unwrap(),
                payload["type"].as_str().unwrap()
            );
            let map = LISTENERS.lock().unwrap();
            if let Some(cb) = map.get(&id) {
                cb(payload);
            }
        }
    });
    let mut client = CLIENT.lock().unwrap();
    client.listen_as_http(callback, "/sse");
}

fn listen_async() {
    thread::spawn(|| listen());
}
