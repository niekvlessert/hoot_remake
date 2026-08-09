#pragma once

#include <string>
#include <string_view>

namespace hoot {

// Normalize an XML byte stream to UTF-8 according to its XML declaration.
// Modern Hoot XML is UTF-8, while legacy catalogues commonly use Shift_JIS.
bool normalize_xml_to_utf8(std::string_view input, std::string& output, std::string& error);

} // namespace hoot
