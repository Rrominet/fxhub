<?php

class TcpClient {
    private $host;
    private $port;
    private $socket = null;

    public function __construct($url) {
        list($this->host, $this->port) = explode(":", $url);
        $this->port = (int)$this->port;
    }

    private function connect() {
        if ($this->socket === null) {
            $this->socket = fsockopen($this->host, $this->port, $errno, $errstr, 5);
            if (!$this->socket) {
                throw new Exception("Connection failed: $errstr ($errno)");
            }
        }
    }

    public function sendAsHttp($data, $path) {
        $this->connect();
        $body = json_encode($data);
        $req =
            "POST $path HTTP/1.1\r\n" .
            "Host: $this->host\r\n" .
            "Content-Type: application/json\r\n" .
            "Content-Length: " . strlen($body) . "\r\n\r\n" .
            $body;

        fwrite($this->socket, $req);

        $res = "";
        while (!feof($this->socket)) {
            $chunk = fgets($this->socket, 4096);
            if ($chunk === false) break;
            $res .= $chunk;
            if (strpos($res, "\r\n\r\n") !== false || strpos($res, "\n\n") !== false) {
                break;
            }
        }
        return $res;
    }

    public function listenAsHttp($callback, $path = "/sse") {
        $this->connect();
        $req =
            "GET $path HTTP/1.1\r\n" .
            "Host: $this->host\r\n" .
            "Accept: text/event-stream\r\n\r\n";

        fwrite($this->socket, $req);

        while (!feof($this->socket)) {
            $line = fgets($this->socket, 4096);
            if ($line === false) break;
            $line = trim($line);
            if ($line !== "") {
                $callback($line);
            }
        }
    }
}

class fxhub {
    private static $url = "localhost:10001";
    private static $client;
    private static $listeners = [];

    private static function client() {
        if (!self::$client) {
            self::$client = new TcpClient(self::$url);
        }
        return self::$client;
    }

    private static function send_http($cmd, $data) {
        $resObj = ["sended" => false];
        $raw = self::client()->sendAsHttp($data, "/" . $cmd);
        $resObj["sended"] = true;

        try {
            $parts = preg_split("/\r\n\r\n|\n\n/", $raw, 2);
            if (count($parts) != 2) {
                $resObj["success"] = false;
                $resObj["error"] = "Error in the response HTTP format.";
                return $resObj;
            }
            $resdata = json_decode($parts[1], true);
            if ($resdata === null) throw new Exception("json decode failed");
            $resObj = array_merge($resObj, $resdata);
        } catch (Exception $e) {
            $resObj["response"] = $raw;
            $resObj["success"] = false;
            $resObj["error"] = "Couldn't parse the response as a JSON";
        }
        return $resObj;
    }

    public static function send_event($app_id, $event_type) {
        $data = ["app-id" => $app_id, "type" => $event_type];
        return self::send_http("send", $data);
    }

    public static function set_state($app_id, $state) {
        $data = ["app-id" => $app_id, "state" => $state];
        return self::send_http("set-state", $data);
    }

    public static function get_state($app_id) {
        $data = ["app-id" => $app_id];
        return self::send_http("state", $data);
    }

    public static function addListener($app_id, $event_type, $callback) {
        $key = $app_id . "_" . $event_type;
        self::$listeners[$key] = $callback;
    }

    public static function listen() {
        $online = function($line) {
            if (substr($line, 0, 5) !== "data:") return;
            $payload = json_decode(substr($line, 5), true);
            if (!$payload || !isset($payload["app-id"]) || !isset($payload["type"])) return;
            $key = $payload["app-id"] . "_" . $payload["type"];
            if (isset(self::$listeners[$key])) {
                $cb = self::$listeners[$key];
                $cb($payload);
            }
        };

        self::client()->listenAsHttp($online, "/sse");
    }
}
