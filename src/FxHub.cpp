#include "./FxHub.h"
#include "debug.h"
#include "files.2/files.h"
#include "./paths.h"
#include "str.h"
#include <mutex>

namespace fxhub
{
    FxHub* _fxhub = nullptr;
}

FxHub::FxHub(int port) : AsyncHttpServer(port), _cmds{}
{
    fxhub::_fxhub = this;
    this->setFuncsByPaths();
    this->setEvents();
}

FxHub::~FxHub()
{
}

void FxHub::setFuncsByPaths()
{	
    auto cmd = this->createCommand("/demo");
    cmd->setJsonExec([this, cmd](const json& args){this->onDemo(cmd);});
    
    cmd = this->createCommand("/send");
    cmd->setMendatoryKeys({"app-id", "type"});
    cmd->setJsonExec([this, cmd](const json& args){this->onSend(cmd);});

    cmd = this->createCommand("/set-state");
    cmd->setMendatoryKeys({"app-id", "state"});
    cmd->setJsonExec([this, cmd](const json& args){this->onSetState(cmd);});

    cmd = this->createCommand("/state");
    cmd->setMendatoryKeys({"app-id"});
    cmd->setJsonExec([this, cmd](const json& args){this->onGetState(cmd);});
}

std::shared_ptr<JsonCommand> FxHub::createCommand(const std::string& path)
{
    auto cmd = _cmds.createCommand<JsonCommand>(path); 	
    this->addJsonFuncByPath(path, [this, path, cmd] (const json& req_body) mutable -> std::string
            {
                try
                {
                    cmd->setJsonArgs(req_body);
                    cmd->exec();
                    return cmd->returnString();
                }
                catch(const std::exception& e)
                {
                    return "{\"success\": false, \"message\": \"" + std::string(e.what()) + "\"}";
                }
            });
    return cmd;
}

// demo implementation to create a basic command
void FxHub::onDemo(std::shared_ptr<JsonCommand> cmd)
{
    this->successCmdJson(cmd, {"type", "demo"});
}

void FxHub::onSend(std::shared_ptr<JsonCommand> cmd)
{
    auto& args = cmd->args<json>();	
    if (args.contains("data"))
        this->sendAppEvent(args["app-id"], args["type"], args["data"]);
    else 
        this->sendAppEvent(args["app-id"], args["type"]);
    this->successCmd(cmd);
}

void FxHub::onSetState(std::shared_ptr<JsonCommand> cmd)
{
    auto& args = cmd->args<json>();
    auto app_id = args["app-id"];
    auto state = args["state"];
    {
        std::lock_guard<std::mutex> lock(_statesMtx);
        _states[app_id] = state;
    }

    this->successCmd(cmd);
}

void FxHub::onGetState(std::shared_ptr<JsonCommand> cmd)
{
    auto& args = cmd->args<json>();
    auto app_id = args["app-id"];
    json state;
    {
        std::lock_guard<std::mutex> lock(_statesMtx);
        state = _states[app_id];
    }

    this->successCmdJson(cmd, state);
}

void FxHub::setEvents()
{
    lg("FxHub::setEvents");
    auto sse = [this](auto& s, auto& httpdata)
    {
        _sseRunning = true;
        auto th_id = std::this_thread::get_id();
        {
            std::lock_guard lk(_appEventsMtx);
            if (_appEventsThreads.find(th_id) == _appEventsThreads.end())
                _appEventsThreads[th_id] = false;
        }

        for(;;)
        {
            std::unique_lock lk(_appEventsMtx);
            _appEventsCv.wait(lk, [this, &th_id]{return !_appEvents.empty() && !_appEventsThreads[th_id];});

            lg("Sending " << _appEvents.size() << " events");
            for (const auto& e : _appEvents)
            {
                json d = e;
                d["time-sended"] = std::chrono::system_clock::now().time_since_epoch().count();
                lg("sending event " << d.dump());
                this->sendAsSSE(s, d);
            }
            _appEventsThreads[th_id] = true;

            bool alldoned = false;
            for (const auto& t : _appEventsThreads)
            {
                alldoned = t.second;
                if (!alldoned)
                    break;
            }

            if (alldoned)
            {
                _appEvents.clear();
                for (const auto& t : _appEventsThreads)
                    _appEventsThreads[t.first] = false;
            }
        }
    };

    this->setOnSSE(sse);
}

void FxHub::sendAppEvent(const std::string& app_id,const std::string& type,const json& data)
{
    if (!_sseRunning)
        return;
    json _toSend;	
    _toSend["app-id"] = app_id;
    _toSend["type"] = type;
    _toSend["data"] = data;
    _toSend["time-emitted"] = std::chrono::system_clock::now().time_since_epoch().count();
    {
        std::lock_guard<std::mutex> lk(_appEventsMtx);
        _appEvents.push_back(_toSend);
    }
    _appEventsCv.notify_all();
}

FxHub* fxhub::get(){return _fxhub;}

FxHub* fxhub::create(int argc, char *argv[])
{
    std::string error;
    if (argc < 2)
    {
        error += "Usage: " + std::string(argv[0]) + " <port>\n";
        error += "No port given.";
        throw std::invalid_argument(error);
    }

    int port = 0;
    try
    {
        port = std::stoi(argv[1]);
    }
    catch(const std::exception& e)
    {
        error += "Failed to convert the port to a integer...\n";
        error += "Port given " + std::string(argv[1]) + "\n";
        error += "Error : " + std::string(e.what()) + "\n";
        throw std::invalid_argument(error);
    }
    return new FxHub(port);
}

