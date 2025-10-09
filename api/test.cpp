#include "./fxhub.h"

int main()
{
    fxhub::addListener("fxplorer", "file-deleted", [](json& data){std::cout << data.dump(4) << std::endl;});
    fxhub::listen();
}
