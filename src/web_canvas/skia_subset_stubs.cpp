// Firefox's Skia archive carries SkPDFSubsetFont, which calls HarfBuzz's
// subsetter. The product HarfBuzz is built with HB_NO_SUBSET and canvas never
// produces PDF, so these entry points report failure the way an unavailable
// subsetter does (SkPDFSubsetFont then embeds nothing).
extern "C" {
// Opaque HarfBuzz handles; only the C symbol names matter to the linker.
struct hb_face_t;
struct hb_set_t;
hb_set_t* hb_set_get_empty();
struct hb_subset_input_t;
struct hb_subset_plan_t;
hb_subset_input_t* hb_subset_input_create_or_fail() { return nullptr; }
void hb_subset_input_destroy(hb_subset_input_t*) {}
hb_set_t* hb_subset_input_glyph_set(hb_subset_input_t*) { return hb_set_get_empty(); }
void hb_subset_input_set_flags(hb_subset_input_t*, unsigned) {}
hb_face_t* hb_subset_or_fail(hb_face_t*, const hb_subset_input_t*) { return nullptr; }
}
