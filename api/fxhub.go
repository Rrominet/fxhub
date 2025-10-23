package fxhub

import (
	"bufio"
	"encoding/json"
	"fmt"
	"net"
	"strings"
	"sync"
)

var (
	url           = "localhost:10001"
	client        = NewTcpClient(url)
	clientWriteMu sync.Mutex
	listeners     = make(map[string]func(map[string]interface{}))
	listenersMu   sync.Mutex
)

// --- TcpClient replacement ---
type TcpClient struct {
	host string
	port string
	conn net.Conn
	mu   sync.Mutex
}

func NewTcpClient(url string) *TcpClient {
	parts := strings.Split(url, ":")
	return &TcpClient{host: parts[0], port: parts[1]}
}

func (c *TcpClient) connect() error {
	if c.conn == nil {
		conn, err := net.Dial("tcp", c.host+":"+c.port)
		if err != nil {
			return err
		}
		c.conn = conn
	}
	return nil
}

func (c *TcpClient) SendAsHttp(data map[string]interface{}, path string) (string, error) {
	if err := c.connect(); err != nil {
		return "", err
	}
	body, _ := json.Marshal(data)
	req := fmt.Sprintf("POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s",
		path, c.host, len(body), string(body))
	c.mu.Lock()
	defer c.mu.Unlock()
	_, err := c.conn.Write([]byte(req))
	if err != nil {
		return "", err
	}
	reader := bufio.NewReader(c.conn)
	var sb strings.Builder
	for {
		line, err := reader.ReadString('\n')
		if err != nil {
			break
		}
		sb.WriteString(line)
		if strings.Contains(sb.String(), "\r\n\r\n") {
			break
		}
	}
	return sb.String(), nil
}

func (c *TcpClient) ListenAsHttp(callback func(string), path string) error {
	if err := c.connect(); err != nil {
		return err
	}
	req := fmt.Sprintf("GET %s HTTP/1.1\r\nHost: %s\r\nAccept: text/event-stream\r\n\r\n", path, c.host)
	c.mu.Lock()
	_, err := c.conn.Write([]byte(req))
	c.mu.Unlock()
	if err != nil {
		return err
	}
	reader := bufio.NewScanner(c.conn)
	for reader.Scan() {
		callback(reader.Text())
	}
	return nil
}

// --- fxhub functions ---

func SendHttp(cmd string, data map[string]interface{}) map[string]interface{} {
	res := make(map[string]interface{})
	res["sended"] = false
	clientWriteMu.Lock()
	raw, err := client.SendAsHttp(data, "/"+cmd)
	clientWriteMu.Unlock()
	res["sended"] = true
	if err != nil {
		res["success"] = false
		res["error"] = err.Error()
		return res
	}
	parts := strings.SplitN(raw, "\r\n\r\n", 2)
	if len(parts) != 2 {
		parts = strings.SplitN(raw, "\n\n", 2)
	}
	if len(parts) != 2 {
		res["success"] = false
		res["error"] = "bad HTTP format"
		return res
	}
	var parsed map[string]interface{}
	if err := json.Unmarshal([]byte(parts[1]), &parsed); err != nil {
		res["success"] = false
		res["error"] = "could not parse JSON"
		return res
	}
	for k, v := range parsed {
		res[k] = v
	}
	return res
}

func SendEvent(appId, eventType string, async bool, callback func(map[string]interface{})) map[string]interface{} {
	f := func() map[string]interface{} {
		data := map[string]interface{}{"app-id": appId, "type": eventType}
		res := SendHttp("send", data)
		if callback != nil {
			callback(res)
		}
		return res
	}
	if async {
		go f()
		return nil
	}
	return f()
}

func SetState(appId string, state interface{}, async bool, callback func(map[string]interface{})) map[string]interface{} {
	f := func() map[string]interface{} {
		data := map[string]interface{}{"app-id": appId, "state": state}
		res := SendHttp("set-state", data)
		if callback != nil {
			callback(res)
		}
		return res
	}
	if async {
		go f()
		return nil
	}
	return f()
}

func GetState(appId string, async bool, callback func(map[string]interface{})) map[string]interface{} {
	f := func() map[string]interface{} {
		data := map[string]interface{}{"app-id": appId}
		res := SendHttp("state", data)
		if callback != nil {
			callback(res)
		}
		return res
	}
	if async {
		go f()
		return nil
	}
	return f()
}

func AddListener(appId, eventType string, cb func(map[string]interface{})) {
	listenersMu.Lock()
	defer listenersMu.Unlock()
	listeners[appId+"_"+eventType] = cb
}

func Listen() {
	online := func(line string) {
		if !strings.HasPrefix(line, "data:") {
			return
		}
		var payload map[string]interface{}
		if err := json.Unmarshal([]byte(line[5:]), &payload); err != nil {
			return
		}
		id := payload["app-id"].(string) + "_" + payload["type"].(string)
		listenersMu.Lock()
		cb := listeners[id]
		listenersMu.Unlock()
		if cb != nil {
			cb(payload)
		}
	}
	_ = client.ListenAsHttp(online, "/sse")
}

func ListenAsync() {
	go Listen()
}
