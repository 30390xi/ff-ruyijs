// Canonical live CSSOM rule state. Native wrappers contain handles, not copies.
#pragma once
#include "domain/styling/media_list_value.h"
#include <memory>
#include <string>
#include <vector>
#include <variant>
#include "domain/styling/css_declarations.h"
namespace ruyijs::domain::styling {
// Internal kinds of the cascade layer rules; CSSRule.type reads 0 for both.
// `selector` holds the layer name (block) or the comma-separated name list.
constexpr int kCssLayerBlock=100,kCssLayerStatement=101;
// @property: `selector` is the custom property name, `condition` holds the
// descriptors as "syntax\x1finherits\x1finitial-value" (each "" when absent).
constexpr int kCssPropertyRule=102;
struct CssPropertyDescriptors { std::string syntax,inherits,initial; bool valid=false; };
// The descriptors of a kCssPropertyRule row; `valid` per css-properties-values
// §3.1 (syntax and inherits present, initial-value present unless syntax "*").
CssPropertyDescriptors CssPropertyRuleDescriptors(const std::string& condition);
struct CssomRuleValue {
  int kind=1;
  bool nested=false;
  std::string selector,condition;
  // One authority: normal rule text or ordered page descriptor rows.
  std::variant<std::string,std::vector<CssDeclarationValue>> declarations;
  std::string& StringDeclarations() {return std::get<std::string>(declarations);}
  const std::string& StringDeclarations() const {return std::get<std::string>(declarations);}
  std::vector<CssDeclarationValue>& PageDeclarations() {return std::get<std::vector<CssDeclarationValue>>(declarations);}
  const std::vector<CssDeclarationValue>& PageDeclarations() const {return std::get<std::vector<CssDeclarationValue>>(declarations);}
  std::string DeclarationText() const;
  // Present only for kind 8: exact f32 percentages determine find/delete
  // identity independently of the rounded CSSOM serialization.
  std::vector<float> key_positions;
  MediaListValue media;
  std::vector<std::shared_ptr<CssomRuleValue>> children;
  std::string ConditionText() const { return kind==4?media.text():condition; }
  std::string Text() const;
};
struct CssRuleParseResult {
  std::vector<std::shared_ptr<CssomRuleValue>> rules;
  bool valid=true;
};
CssRuleParseResult ParseCssRuleValues(const std::string&,bool strict=false);
}
