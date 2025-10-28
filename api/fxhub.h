#pragma once
#include "str.h"
#include "debug.h"
#include "network/TcpClient.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include "thread.h"

//to be able to use this program you need to include the mlapi dir
//you need to link it against the mlapi shared lib (libmlapi.so on linux in /usr/local/lib or /usr/lib depending of your install)

namespace fxhub
{
    inline std::string& url() {static std::string url = "localhost:10001"; return url;}
    inline std::mutex& clientWriteMtx() {static std::mutex mtx; return mtx;}
    inline TcpClient& client() {static TcpClient client(url()); return client;}
    inline th::ThreadPool& ths() {static th::ThreadPool ths; return ths;}
    inline std::mutex& listenersMtx() {static std::mutex mtx; return mtx;}
    inline std::unordered_map<std::string, std::function<void(json& data)>>& listeners() {static std::unordered_map<std::string, std::function<void(json& data)>> listeners; return listeners;}

    inline json send_http(const std::string& cmd, const json& data)
    {
        json _r;    
        json _resdata;
        std::string res;
        _r["sended"] = false;
        {
            std::lock_guard<std::mutex> lk(clientWriteMtx());
            lg("Sending : " + cmd);
            lg("With data : " + data.dump(4));
            try
            {
                res = client().sendAsHttp(data, "/" + cmd);
            }
            catch(const std::exception& e)
            {
                lg("Error in fxhub sendind data to the server : " << e.what());
            }
        }
        _r["sended"] = true;
        try
        {
            auto _tmparr = str::split(res, "\r\n\r\n");
            if (_tmparr.size() != 2)
                _tmparr = str::split(res, "\n\n");
            if (_tmparr.size() != 2)
            {
                _r["success"] = false;
                _r["error"] = "Error in the response HTTP format.";
                return _r;
            }
            auto _tmpdata = _tmparr[1];
            _resdata = json::parse(_tmpdata); 
        }
        catch(const std::exception& e)
        {
            _r["response"] = res;
            _r["success"] = false;
            _r["error"] = "Couldn't parse the response as a JSON";
        }

        for (auto& item : _resdata.items())
            _r[item.key()] = item.value();

        return _r;
    }

    //if async is true, the returned json will be null
    //You get it with the callback
    inline json send_event(const std::string& app_id, const std::string& event_type, const json& data=json(), bool async=true, const std::function<void(const json& )>& callback=0)
    {
        auto f = [=](){
            json _data;
            _data["app-id"] = app_id;
            _data["type"] = event_type;
            _data["data"] = data;
            json res = send_http("send", _data);
            if (callback)
                callback(res);
            return res;
        };
        if (async)
            ths().run(f);
        else
            return f();
        return json();
    }

    inline json set_state(const std::string& app_id, const json& state, bool async=true, const std::function<void(const json&)>& callback=0)
    {
        auto f = [=](){
            json data;
            data["app-id"] = app_id;
            data["state"] = state;
            json res = send_http("set-state", data);
            if (callback)
                callback(res);
            return res;
        };
        if (async)
            ths().run(f);
        else
            return f();
        return json();
    }

    inline json get_state(const std::string& app_id, bool async=false, const std::function<void(json& data)>& callback=0)
    {
        auto f = [=](){
            json data;
            data["app-id"] = app_id;
            json res = send_http("state", data);
            if (callback)
                callback(res);
            return res;
        };
        if (async)
            ths().run(f);
        else
            return f();
        return json();
    }

    inline void addListener(const std::string& app_id, const std::string& event_type, const std::function<void(json& data)>& callback)
    {
        std::lock_guard<std::mutex> lk(listenersMtx());
        listeners()[app_id + "_" + event_type] = callback;
    }

    inline void listen()
    {
        auto online = [](const std::string& line)
        {
            if (line.substr(0, 5) != "data:")
                return;
            json data;
            try
            {
                auto data = json::parse(line.substr(5));
            }
            catch(const std::exception& e)
            {
                lg("Error parsing the line into a json : " + line);
                lg(e.what());
                return;
            }
            if (!data.contains("app-id") || !data.contains("type"))
                return;
            std::string id = data["app-id"].get<std::string>() + "_" + data["type"].get<std::string>();
            std::function<void(json&)> callback;
            {
                std::lock_guard<std::mutex> lk(listenersMtx());
                if (listeners().find(id) != listeners().end())
                    callback = listeners()[id];
            }
            callback(data);
        };

        //FIXME : Potential race condition here in the reading socket between classic responses and sse
        //Only valid problem in listen_async
        client().listenAsHttp(online, "/sse"); 
    }

    inline void listen_async()
    {
        ths().run(listen);
    }
}
