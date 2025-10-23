#pragma once
#include "network/AsyncHttpServer.h"
#include "vec.h"
#include <condition_variable>
#include <memory>
#include "Events.h"
#include <mutex>

/*
 * The FxHub server implement a Command pattern to work.
 * It's really easy, to add a command (a peice of code that will react to url request) :
 * Just call createCommand(path) in setFuncsByPaths
 * After you can simply call on the returned cmd shared_ptr : 
 *
 // what key in the json request are mandatory for the command to run
    cmd->setMendatoryKeys({"id", "root"});

 // set the function/method to exec on request that take the cmd shared_ptr itself as arg.
 // in the function you can access the json request body with cmd->args<json>()
 // and you can set the returned json witn cmd->setReturnValue() or quick responses witn FxHub::successCmd/FxHub::failureCmd
    cmd->setJsonExec([this, cmd](const json& args){this->onDemo(cmd);});
 *
 *
 * search for execEvent(...) in this dir to see all possible events.
 */

class FxHub : public AsyncHttpServer
{
    public : 
        FxHub(int port);
        virtual ~FxHub();

        void setFuncsByPaths();
        void setEvents();

        std::shared_ptr<JsonCommand> createCommand(const std::string& path);
        ml::Events& events() {return _events;}

        void sendAppEvent(const std::string& app_id, const std::string& type, const json& data={});

    private : 
        ml::CommandsManager _cmds;
        ml::Events _events;

        std::mutex _statesMtx;
        std::unordered_map<std::string, json> _states;

        std::mutex _appEventsMtx;
        std::condition_variable _appEventsCv;
        ml::Vec<json> _appEvents;
        std::map<std::thread::id, bool> _appEventsThreads;

        std::atomic<bool> _sseRunning{false};
        
    public : 
        void onDemo(std::shared_ptr<JsonCommand> cmd);

        // listen to any event from any given app
        void onSend(std::shared_ptr<JsonCommand> cmd);
        void onSetState(std::shared_ptr<JsonCommand> cmd);
        void onGetState(std::shared_ptr<JsonCommand> cmd);
};

namespace fxhub
{
    FxHub* get();
    FxHub* create(int argc, char *argv[]);
}
