#include "./paths.h"
#include "files.2/files.h"
#include <cassert>
#include "str.h"

namespace paths
{
    bool _initCalled = false;
    void init()
    {
        if (_initCalled)
            return;

        _initCalled = true;
        db_write("paths initialized.");
    }
}
