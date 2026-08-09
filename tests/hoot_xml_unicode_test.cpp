#include <cassert>
#include <string>

#include "config/hoot_catalog.h"
#include "config/hoot_xml_loader.h"

int main()
{
    const std::string xml = u8R"XML(<?xml version="1.0" encoding="UTF-8"?>
<hoot>
  <game>
    <name>秘密諜報部員&#x30BC;&#12525;ゼロななこちゃん &amp; 日本</name>
    <driver type="opn">pc98dos</driver>
    <romlist archive="unicode-test"><rom>TEST.M</rom></romlist>
    <titlelist>
      <title code="0">ななこの&#65297;日 &amp; テスト</title>
    </titlelist>
  </game>
</hoot>)XML";

    hoot::HootCatalog catalog;
    std::string error;
    hoot::HootXmlLoader loader;
    assert(loader.load_string(xml, catalog, error));
    assert(error.empty());
    assert(catalog.entries().size() == 1);
    const auto& entry = catalog.entries().front();
    assert(entry.title == u8"秘密諜報部員ゼロゼロななこちゃん & 日本");
    assert(entry.driver_name == "pc98dos/opn");
    assert(entry.tracks.size() == 1);
    assert(entry.tracks.front().title == u8"ななこの１日 & テスト");
    return 0;
}
