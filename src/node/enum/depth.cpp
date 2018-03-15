#include "depth.hpp"

#include "enum.hpp"

#define SET_DEPTH(name) SET_ENUM(object, svn::depth, name)

namespace no {
namespace depth {
void init(v8::Local<v8::Object>&  exports,
          v8::Isolate*            isolate,
          v8::Local<v8::Context>& context) {
    auto object = no::New<v8::Object>(isolate);

    SET_DEPTH(unknown);
    SET_DEPTH(empty);
    SET_DEPTH(files);
    SET_DEPTH(immediates);
    SET_DEPTH(infinity);

    SET_READONLY(exports, "Depth", object);
}
} // namespace depth
} // namespace no
