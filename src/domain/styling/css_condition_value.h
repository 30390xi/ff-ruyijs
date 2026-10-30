#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <string>
#include <string_view>
namespace ruyijs::domain::styling {
struct CssConditionResult { bool valid=false,value=false,known=true; };
using CssConditionAtom=std::function<CssConditionResult(std::string_view)>;
CssConditionResult CssBooleanCondition(std::string_view,const CssConditionAtom&,unsigned depth=0);
struct CssMediaEnvironment {
  double width=0,height=0,deviceWidth=0,deviceHeight=0,ratio=1,color=8;
  bool touch=false;
  // Configured media-feature answers keyed by CSS feature name; absent keys keep the built-in answer.
  std::map<std::string,std::string> profile;
  // Cascade layers of one cascade read as dotted names (a, a.b), in order of
  // first declaration across its sheets: a rule stores its layer's index here
  // and the cascade derives the layer order from these names (see
  // CssLayerOrder). Shared by every sheet projected for that read.
  std::shared_ptr<std::vector<std::string>> layers=std::make_shared<std::vector<std::string>>();
  // Registered custom properties (@property) of the read: name -> (inherits,
  // initial value). The last valid registration in cascade order wins.
  std::shared_ptr<std::map<std::string,std::pair<bool,std::string>>> registered=
      std::make_shared<std::map<std::string,std::pair<bool,std::string>>>();
};
bool CssMediaCondition(const std::string&,const CssMediaEnvironment&);
CssConditionResult CssMediaFeature(std::string_view,const CssMediaEnvironment&);
CssConditionResult CssMediaQuery(const std::string&,const CssMediaEnvironment&);
std::string CssNormalizeMediaQuery(const std::string&,bool* valid=nullptr);
bool CssSupportsProperty(const std::string&,const std::string&);
bool CssSupportsCondition(const std::string&);
}
