// Interface objects and prototypes for CSS rule interfaces Firefox exposes but
// the parser never instantiates: members only answer the brand failure with
// Firefox's text. The member name is the accessor's own "get "/"set " name, so
// one shared native serves every accessor and every interface (indexed by slot).
#include "web_styling/rule_interface_members.h"
#include "js/Wrapper.h"
#include "jsfriendapi.h"
#include "js/Array.h"
#include "web_styling/cssom.h"
#include "domain/styling/css_components.h"
#include <cstddef>

namespace ruyijs::web_styling {
const JSClass kRuleInterfaceClass = {"CSSRuleInterface", JSCLASS_HAS_RESERVED_SLOTS(1)};

namespace {
constexpr const char* kInterfaceNames[SlotCount] = {
    "CSSContainerRule", "CSSFontFaceRule", "CSSFontFeatureValuesRule", "CSSImportRule",
    "CSSLayerBlockRule", "CSSLayerStatementRule", "CSSNamespaceRule", "CSSPositionTryRule",
    "CSSPropertyRule", "CSSScopeRule", "CSSStartingStyleRule", "CSSFontFaceDescriptors",
    "CSSPositionTryDescriptors"};
}
// CSSLayerBlockRule.name and CSSLayerStatementRule.nameList read the layer
// rule the parser built; every other member keeps the brand failure.
bool LayerMember(JSContext* cx, const JS::CallArgs& args, int slot, bool* handled) {
  *handled = false;
  if (!args.thisv().isObject()) return true;
  JS::RootedObject self(cx, js::CheckedUnwrapStatic(&args.thisv().toObject()));
  if (!self) return true;
  const JSClass* c = JS::GetClass(self);
  if (slot == SlotCSSPropertyRule && c == &kPropertyRuleClass) {
    // CSSPropertyRule: name, syntax (unquoted), inherits, initialValue (null
    // when absent). The accessor's own name selects the member.
    *handled = true;
    const auto& row = RuleDataOf(self);
    const auto d = ruyijs::domain::styling::CssPropertyRuleDescriptors(row.condition);
    JSFunction* fun = JS_GetObjectFunction(&args.callee());
    JS::RootedString nameString(cx, fun ? JS_GetMaybePartialFunctionId(fun) : nullptr);
    if (!nameString) { *handled = false; return true; }
    auto is = [&](const char* text) {
      bool match = false;
      return JS_StringEqualsAscii(cx, nameString, text, &match) && match;
    };
    std::string text;
    if (is("get name")) text = row.selector;
    else if (is("get syntax")) text = d.syntax.size() >= 2 ? d.syntax.substr(1, d.syntax.size() - 2) : "";
    else if (is("get inherits")) { args.rval().setBoolean(d.inherits == "true"); return true; }
    else if (is("get initialValue")) {
      if (d.initial.empty()) { args.rval().setNull(); return true; }
      text = d.initial;
    } else { *handled = false; return true; }
    JS::RootedString out(cx, JS_NewStringCopyUTF8N(cx, JS::UTF8Chars(text.data(), text.size())));
    if (!out) return false;
    args.rval().setString(out);
    return true;
  }
  if (!((slot == SlotCSSLayerBlockRule && c == &kLayerBlockRuleClass) ||
        (slot == SlotCSSLayerStatementRule && c == &kLayerStatementRuleClass))) return true;
  *handled = true;
  const std::string& names = RuleDataOf(self).selector;
  if (slot == SlotCSSLayerBlockRule) {
    JS::RootedString text(cx, JS_NewStringCopyUTF8N(cx, JS::UTF8Chars(names.data(), names.size())));
    if (!text) return false;
    args.rval().setString(text);
    return true;
  }
  JS::RootedValueVector items(cx);
  for (const auto& part : ruyijs::domain::styling::CssSplit(names, ',')) {
    const auto name = ruyijs::domain::styling::CssTrim(part);
    JSString* text = JS_NewStringCopyUTF8N(cx, JS::UTF8Chars(name.data(), name.size()));
    if (!text || !items.append(JS::StringValue(text))) return false;
  }
  JS::RootedObject array(cx, JS::NewArrayObject(cx, items));
  if (!array || !JS_FreezeObject(cx, array)) return false;
  args.rval().setObject(*array);
  return JS_WrapValue(cx, args.rval());
}
bool RuleInterfaceGet(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  bool handled = false;
  if (!LayerMember(cx, args, WebKit::Slot(args), &handled)) return false;
  if (handled) return true;
  return WebKit::FailBrand(cx, args, kInterfaceNames[WebKit::Slot(args)]);
}
bool RuleInterfaceSet(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  return WebKit::FailBrand(cx, args, kInterfaceNames[WebKit::Slot(args)]);
}
bool DefineRuleInterface(State& state, RuleInterfaceSlot slot, JS::HandleObject parentProto,
                         JS::HandleObject parentCtor, WebFace& face,
                         const RuleInterfaceMember* members, size_t count) {
  auto& kit = state.kit;
  const char* iface = kInterfaceNames[slot];
  if (!kit.Define(iface, &kRuleInterfaceClass, parentProto, WebKit::Reject, &face) ||
      !JS_SetPrototype(kit.cx, face.constructor, parentCtor)) return false;
  for (size_t i = 0; i < count; ++i)
    if (!kit.Accessor(face, iface, members[i].name, RuleInterfaceGet,
                      members[i].writable ? RuleInterfaceSet : nullptr, int32_t(slot)))
      return false;
  return OrderConstructor(kit, face);
}
}
