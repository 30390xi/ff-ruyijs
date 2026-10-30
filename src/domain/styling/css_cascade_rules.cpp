// Project the single live rule tree into the existing per-compute cascade input.
#include "domain/styling/stylesheet.h"
namespace ruyijs::domain::styling {
namespace {
std::string NestedSelector(const std::string& input,const std::string& parent) {
  if(parent.empty()) return input;
  const std::string ancestor=":is("+parent+")";std::string result;
  for(const auto& group:CssSplit(input,',')) {
    std::string current;bool ampersand=false;char quote=0;
    for(size_t i=0;i<group.size();++i) {
      char c=group[i];
      if(c=='\\') {current+=c;if(i+1<group.size()) current+=group[++i];continue;}
      if(quote) {current+=c;if(c==quote) quote=0;continue;}
      if(c=='\''||c=='"') {quote=c;current+=c;continue;}
      if(c=='&') {current+=ancestor;ampersand=true;}else current+=c;
    }
    if(!result.empty()) result+=", ";
    result+=(ampersand?"":ancestor+" ")+current;
  }
  return result;
}
// The index of a (fully qualified) layer name, declaring it on first use;
// declaring a.b also declares a first.
uint32_t LayerRank(const CssMediaEnvironment& environment,const std::string& name) {
  if(const auto dot=name.rfind('.');dot!=std::string::npos) LayerRank(environment,name.substr(0,dot));
  auto& layers=*environment.layers;
  for(uint32_t i=0;i<layers.size();++i) if(layers[i]==name) return i;
  layers.push_back(name);
  return uint32_t(layers.size()-1);
}
// An anonymous @layer block is its own layer, distinct from every other.
std::string AnonymousLayer(const CssomRuleValue* row) {
  char buffer[32];std::snprintf(buffer,sizeof buffer,"#%p",static_cast<const void*>(row));
  return buffer;
}
void Project(StyleSheetAggregate& sheet,const std::vector<std::shared_ptr<CssomRuleValue>>& rows,
              const CssMediaEnvironment& environment,const std::string& parent,unsigned depth,
              const std::string& layer) {
  if(depth>64||parent.size()>8*1024*1024) return;
  const auto qualified=[&](const std::string& name) { return layer.empty()?name:layer+"."+name; };
  for(const auto& row:rows) {
    if(row->kind==4||row->kind==12) {
      bool applies=row->kind==4?CssMediaCondition(row->media.text(),environment):CssSupportsCondition(row->condition);
      if(applies) Project(sheet,row->children,environment,parent,depth+1,layer);
      continue;
    }
    if(row->kind==kCssPropertyRule) {
      const auto d=CssPropertyRuleDescriptors(row->condition);
      if(d.valid) (*environment.registered)[row->selector]={d.inherits=="true",d.initial};
      continue;
    }
    if(row->kind==kCssLayerStatement) {
      for(const auto& name:CssSplit(row->selector,',')) LayerRank(environment,qualified(CssTrim(name)));
      continue;
    }
    if(row->kind==kCssLayerBlock) {
      const auto name=qualified(row->selector.empty()?AnonymousLayer(row.get()):row->selector);
      LayerRank(environment,name);
      Project(sheet,row->children,environment,parent,depth+1,name);
      continue;
    }
    if(row->kind!=1&&row->kind!=0) continue;
    std::string selector=row->kind==0?parent:NestedSelector(row->selector,parent);
    if(!selector.empty()&&!row->StringDeclarations().empty()) {
      CssRule rule(CssRule::Type::Style,CssSelector{selector});
      if(!layer.empty()) rule.set_layer(LayerRank(environment,layer));
      for(const auto& property:CssDeclarations(row->StringDeclarations()))
        rule.set_property(property.name,property.value,property.important);
      sheet.add_rule(std::move(rule));
    }
    Project(sheet,row->children,environment,selector,depth+1,layer);
  }
}
}
void StyleSheetAggregate::append_rule_values(const std::vector<std::shared_ptr<CssomRuleValue>>& rows,
                                            const CssMediaEnvironment& environment) {
  layers=environment.layers;
  registered=environment.registered;
  Project(*this,rows,environment,"",0,"");
}
}
