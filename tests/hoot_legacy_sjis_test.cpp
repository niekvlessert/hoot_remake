#include <cassert>
#include <string>

#include "config/hoot_catalog.h"
#include "config/hoot_xml_loader.h"

int main(int argc, char** argv)
{
    assert(argc == 2);
    hoot::HootCatalog catalog;
    std::string error;
    hoot::HootXmlLoader loader;
    assert(loader.load_file(argv[1], catalog, error));
    assert(error.empty());
    assert(!catalog.entries().empty());

    bool found = false;
    for (const auto& entry : catalog.entries()) {
        if (entry.title == u8"ロードモナーク (スピークボード専用体験版)") {
            found = true;
            break;
        }
    }
    assert(found);
    return 0;
}
