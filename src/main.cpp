#include "debug.h"
#include "files.2/files.h"
#include <memory>
#include "./FxHub.h"
#include "./paths.h"

void initLogFile()
{
#ifdef NO_LOG
#else 
    db::setLogFile(files::execDir() + files::sep() + "fxhub.log");
#endif
}

int main(int argc, char *argv[]) 
{
    initLogFile();
    paths::init();
    db_write("Starting the task-tracker server...");
    std::unique_ptr<FxHub> server;
    try
    {
        server = std::unique_ptr<FxHub>(fxhub::create(argc, argv));
    }
    catch(const std::exception& e)
    {
        db_write2("Failed to create server", e.what());
        abort();
    }

    server->run();
    return 0;
}

