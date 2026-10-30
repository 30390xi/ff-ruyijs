#include "domain/styling/css_rule_value.h"
#include "domain/styling/css_components.h"
#include "domain/styling/css_keyframes.h"
#include "domain/styling/css_page_value.h"
namespace ruyijs::domain::styling {
std::string CssomRuleValue::DeclarationText() const {
  return kind==6?CssPageText(PageDeclarations()):StringDeclarations();
}
CssPropertyDescriptors CssPropertyRuleDescriptors(const std::string& condition) {
  CssPropertyDescriptors out;
  const auto a=condition.find('\x1f'),b=a==std::string::npos?a:condition.find('\x1f',a+1);
  if(b==std::string::npos) return out;
  out.syntax=condition.substr(0,a);out.inherits=condition.substr(a+1,b-a-1);out.initial=condition.substr(b+1);
  const bool universal=out.syntax=="\"*\""||out.syntax=="'*'";
  out.valid=!out.syntax.empty()&&(out.inherits=="true"||out.inherits=="false")&&(universal||!out.initial.empty());
  return out;
}
std::string CssomRuleValue::Text() const {
  if(kind==0) return StringDeclarations();
  if(kind==6) {
    const auto prefix=std::string("@page")+(selector.empty()?"":" "+selector);
    const auto block=CssPageText(PageDeclarations());
    if(children.empty()) return prefix+" { "+block+(block.empty()?"":" ")+"}";
    std::string result=prefix+" {";
    if(!block.empty()) result+="\n  "+block;
    for(const auto& child:children) result+="\n  "+child->Text();
    return result+"\n}";
  }
  if(kind==7) {
    std::string result="@keyframes "+CssKeyframesNameText(selector)+" {";
    for(const auto& child:children) result+="\n"+child->Text();
    return result+"\n}";
  }
  if(kind==8) return selector+" { "+StringDeclarations()+" }";
  if(kind==kCssLayerStatement) return "@layer "+selector+";";
  if(kind==kCssPropertyRule) {
    const auto d=CssPropertyRuleDescriptors(condition);
    std::string body;
    if(!d.syntax.empty()) body+=" syntax: "+d.syntax+";";
    if(!d.inherits.empty()) body+=" inherits: "+d.inherits+";";
    if(!d.initial.empty()) body+=" initial-value: "+d.initial+";";
    return "@property "+selector+" {"+body+" }";
  }
  if(kind==kCssLayerBlock) {
    std::string result="@layer"+(selector.empty()?std::string():" "+selector)+" {";
    for(const auto& child:children) result+="\n  "+child->Text();
    return result+"\n}";
  }
  if(kind==1&&children.empty()) return selector+" { "+StringDeclarations()+(StringDeclarations().empty()?"":" ")+"}";
  std::string result=kind==1?selector:kind==4?"@media "+media.text():"@supports "+condition;
  result+=" {";
  if(kind==1&&!StringDeclarations().empty()) result+="\n  "+StringDeclarations();
  for(const auto& child:children) result+="\n  "+child->Text();
  return result+"\n}";
}
}
