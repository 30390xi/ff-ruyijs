// Firefox (Servo) specified-value serialization of one declaration: the value
// a declaration block stores and every CSSOM read (cssText, getPropertyValue)
// returns. Servo parses a declaration and re-serializes the parsed value, so
// the authored text is not kept: #fff reads rgb(255, 255, 255), .5 reads 0.5,
// a unitless 0 length reads 0px, and shorthands such as animation, transition,
// box-shadow and outline are written in their canonical component order.
#pragma once
#include <string>
#include <string_view>
namespace ruyijs::domain::styling {
// The serialized value of `name: value` (an already lowercased, canonical
// property name). Values with var()/env() references keep their authored
// token stream, as Servo stores them unparsed. Idempotent.
std::string CssSpecifiedValue(std::string_view name, std::string_view value);
// Whether Firefox 159 parses this property at all (CSS2Properties names, and
// custom properties). Declarations of any other name are dropped.
bool CssKnownProperty(std::string_view name);
// The property a dash-case alias stands for (-webkit-appearance -> appearance),
// empty for a name that is not an alias.
std::string CssPropertyAliasTarget(std::string_view name);
}  // namespace ruyijs::domain::styling
