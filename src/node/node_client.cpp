#include "node_client.hpp"

#include <cstring>

#include <node_buffer.h>

#include <uv/async.hpp>
#include <uv/work.hpp>

#include <cpp/client.hpp>
#include <cpp/svn_type_error.hpp>

#include <node/error.hpp>
#include <node/iterable.hpp>
#include <node/type_conversion.hpp>
#include <objects/class_builder.hpp>

// clang-format off

#define REPORT_ERROR                                  \
    } catch (const svn::svn_error& raw_error) {       \
        auto _Error = copy_error(isolate, raw_error); \
        _Resolver->Reject(_Error);                    \
    }

#define METHOD_BEGIN(name)                                                               \
    v8::Local<v8::Value> client::name(const v8::FunctionCallbackInfo<v8::Value>& args) { \
        auto isolate = args.GetIsolate();                                                \
        auto context = isolate->GetCurrentContext();                                     \
                                                                                         \
        auto _Resolver = no::data<v8::Promise::Resolver>(context);                       \
                                                                                         \
        try {

#define EXPAND(x) x

#ifdef __GNUC__
#define NUM_ARGS_IMPL(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N
#define NUM_ARGS(...) NUM_ARGS_IMPL(_, ##__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#else
#define NUM_ARGS_COUNT(x0, x1, x2, x3, x4, x5, n, ...) n
#define NUM_ARGS_PAD(...) 0, __VA_ARGS__
#define NUM_ARGS_EXPAND(...) EXPAND(NUM_ARGS_COUNT(__VA_ARGS__, 5, 4, 3, 2, 1, 0))
#define NUM_ARGS(...) NUM_ARGS_EXPAND(NUM_ARGS_PAD(__VA_ARGS__))
#endif

#define CAPTURE_0()
#define CAPTURE_1(x) x = std::move(x),
#define CAPTURE_2(x, ...) CAPTURE_1(x) CAPTURE_1(__VA_ARGS__)
#define CAPTURE_3(x, ...) CAPTURE_1(x) EXPAND(CAPTURE_2(__VA_ARGS__))
#define CAPTURE_4(x, ...) CAPTURE_1(x) EXPAND(CAPTURE_3(__VA_ARGS__))
#define CAPTURE_5(x, ...) CAPTURE_1(x) EXPAND(CAPTURE_4(__VA_ARGS__))
#define CAPTURE_N(n, ...) EXPAND(CAPTURE_##n(__VA_ARGS__))
#define CAPTURE_EXPEND(n, ...) CAPTURE_N(n, __VA_ARGS__)
#define CAPTURE(...) CAPTURE_EXPEND(NUM_ARGS(__VA_ARGS__), __VA_ARGS__)

#define ASYNC_BEGIN(...) \
    auto _Work = [CAPTURE(__VA_ARGS__) this]() -> auto {

#define ASYNC_END(...)                                                                                                                      \
    };                                                                                                                                      \
                                                                                                                                            \
    v8::Global<v8::Promise::Resolver> __Resolver(isolate, _Resolver);                                                                       \
    auto _After_work = [CAPTURE(__VA_ARGS__) isolate, __Resolver = std::move(__Resolver)](std::future<decltype(_Work())> _Future) -> void { \
        v8::HandleScope _Scope(isolate);                                                                                                    \
		auto context = isolate->GetCurrentContext();                                                                                        \
                                                                                                                                            \
        auto _Resolver = __Resolver.Get(isolate);                                                                                           \
        try {                                                                                                                               \

#define ASYNC_RESULT \
    _Future.get()

#define METHOD_RETURN(value)                                          \
                no::check_result(_Resolver->Resolve(context, value)); \
            REPORT_ERROR;                                             \
        };                                                            \
                                                                      \
        uv::queue_work(std::move(_Work), std::move(_After_work));     \
    REPORT_ERROR;                                                     \
                                                                      \
    return _Resolver;                                                 \
}

// clang-format on

static v8::Local<v8::Value> copy_error(v8::Isolate* isolate, const svn::svn_error& raw_error) {
    auto message = raw_error.what();

    no::object error(v8::Exception::Error(no::data(isolate, message).As<v8::String>()).As<v8::Object>());

    error["name"] = "SvnError";
    error["code"] = raw_error.code;
    error["file"] = raw_error.file;
    error["line"] = raw_error.line;

    if (raw_error.child != nullptr)
        error["child"] = copy_error(isolate, *raw_error.child);

    return error;
}

static std::vector<std::string> convert_array(const v8::Local<v8::Value>& value, bool allowEmpty) {
    if (value->IsUndefined()) {
        if (allowEmpty)
            return std::vector<std::string>();
        else
            throw no::type_error("");
    }

    if (value->IsString())
        return std::vector<std::string>{convert_string(value)};

    if (value->IsArray()) {
        auto array  = value.As<v8::Array>();
        auto length = array->Length();
        auto result = std::vector<std::string>();
        for (uint32_t i = 0; i < length; i++) {
            auto item = array->Get(i);
            result.push_back(std::move(convert_string(item)));
        }
        return result;
    }

    throw no::type_error("");
}

static v8::Local<v8::Object> convert_options(const v8::Local<v8::Value> options) {
    if (options->IsUndefined())
        return v8::Local<v8::Object>();

    if (options->IsObject())
        return options.As<v8::Object>();

    throw no::type_error("");
}

static int32_t convert_number(v8::Isolate*                 isolate,
                              const v8::Local<v8::Object>& options,
                              const char*                  key,
                              int32_t                      defaultValue) {
    if (options.IsEmpty())
        return defaultValue;

    auto value = options->Get(no::data(isolate, key, v8::NewStringType::kInternalized));
    if (value->IsUndefined())
        return defaultValue;

    if (value->IsNumber()) {
        return value->Int32Value();
    }

    throw no::type_error("");
}

static svn::revision convert_revision(v8::Isolate*                 isolate,
                                      const v8::Local<v8::Object>& options,
                                      const char*                  key,
                                      svn::revision                defaultValue) {
    if (options.IsEmpty())
        return defaultValue;

    auto value = options->Get(no::data(isolate, key, v8::NewStringType::kInternalized));
    if (value->IsUndefined())
        return defaultValue;

    if (value->IsNumber()) {
        auto simple = static_cast<svn::revision_kind>(value->Int32Value());
        return simple;
    }

    if (value->IsObject()) {
        no::object object(value.As<v8::Object>());

        auto number = object["number"];
        if (!number->IsUndefined()) {
            if (!number->IsNumber())
                throw no::type_error("");

            return svn::revision(number->Int32Value());
        }

        auto date = object["date"];
        if (!date->IsUndefined()) {
            if (!date->IsNumber())
                throw no::type_error("");

            return svn::revision(date->IntegerValue());
        }
    }

    throw no::type_error("");
}

static svn::revision_range convert_revision_range(v8::Isolate*                isolate,
                                                  const v8::Local<v8::Value>& value) {
    if (value.IsEmpty()) {
        throw no::type_error("");
    }

    if (!value->IsObject()) {
        throw no::type_error("");
    }

    auto object = value.As<v8::Object>();

    auto start = convert_revision(isolate, object, "start", svn::revision_kind::unspecified);
    if (start.kind == svn::revision_kind::unspecified) {
        throw no::type_error("");
    }

    auto end = convert_revision(isolate, object, "end", svn::revision_kind::unspecified);
    if (end.kind == svn::revision_kind::unspecified) {
        throw no::type_error("");
    }

    return svn::revision_range{start, end};
}

static std::optional<std::vector<svn::revision_range>> convert_revision_ranges(v8::Isolate*                 isolate,
                                                                               const v8::Local<v8::Object>& options,
                                                                               const char*                  key) {
    if (options.IsEmpty()) {
        return {};
    }

    auto value = options->Get(no::data(isolate, key, v8::NewStringType::kInternalized));
    if (value->IsUndefined()) {
        return {};
    }

    std::vector<svn::revision_range> result;

    if (value->IsArray()) {
        auto array  = value.As<v8::Array>();
        auto length = array->Length();
        for (uint32_t i = 0; i < length; i++) {
            auto item = array->Get(i);
            result.push_back(std::move(convert_revision_range(isolate, item)));
        }
    } else if (value->IsObject()) {
        result.push_back(std::move(convert_revision_range(isolate, value)));
    }

    return result;
}

static svn::depth convert_depth(v8::Isolate*                 isolate,
                                const v8::Local<v8::Object>& options,
                                const char*                  key,
                                svn::depth                   defaultValue) {
    if (options.IsEmpty()) {
        return defaultValue;
    }

    auto value = options->Get(no::data(isolate, key, v8::NewStringType::kInternalized));
    if (value->IsUndefined()) {
        return defaultValue;
    }

    if (value->IsNumber()) {
        return static_cast<svn::depth>(value->Int32Value());
    }

    throw no::type_error("");
}

static bool convert_boolean(v8::Isolate*                 isolate,
                            const v8::Local<v8::Object>& options,
                            const char*                  key,
                            bool                         defaultValue) {
    if (options.IsEmpty())
        return defaultValue;

    auto value = options->Get(no::data(isolate, key, v8::NewStringType::kInternalized));
    if (value->IsUndefined())
        return defaultValue;

    if (value->IsBoolean())
        return value->BooleanValue();

    throw no::type_error("");
}

static std::vector<std::string> convert_array(v8::Isolate*          isolate,
                                              v8::Local<v8::Object> options,
                                              const char*           key) {
    if (options.IsEmpty())
        return std::vector<std::string>();

    auto value = options->Get(no::data(isolate, key, v8::NewStringType::kInternalized));
    if (value->IsUndefined())
        return std::vector<std::string>();

    return convert_array(value, true);
}

static void buffer_free_pointer(char*, void* hint) {
    delete static_cast<std::vector<char>*>(hint);
}

static v8::Local<v8::Object> buffer_from_vector(v8::Isolate* isolate, std::vector<char>& vector) {
    auto pointer = new std::vector<char>(std::move(vector));
    return node::Buffer::New(isolate,
                             pointer->data(),
                             pointer->size(),
                             buffer_free_pointer,
                             pointer)
        .ToLocalChecked();
}

#define STRINGIFY_INTERNAL(X) #X
#define STRINGIFY(X) STRINGIFY_INTERNAL(X)

namespace no {
void client::initialize(no::object& exports) {
    v8::HandleScope scope(exports.isolate());

    class_builder<client> clazz(exports.isolate(), "Client", constructor);
    clazz.add_prototype_method("add_simple_auth_provider", &client::add_simple_auth_provider, 1);
    clazz.add_prototype_method("remove_simple_auth_provider", &client::remove_simple_auth_provider, 1);

    clazz.add_prototype_method("add_to_changelist", &client::add_to_changelist, 2);
    clazz.add_prototype_method("get_changelists", &client::get_changelists, 2);
    clazz.add_prototype_method("remove_from_changelists", &client::remove_from_changelists, 2);

    clazz.add_prototype_method("add", &client::add, 1);
    clazz.add_prototype_method("blame", &client::blame, 1);
    clazz.add_prototype_method("cat", &client::cat, 1);
    clazz.add_prototype_method("checkout", &client::checkout, 1);
    clazz.add_prototype_method("cleanup", &client::cleanup, 1);
    clazz.add_prototype_method("commit", &client::commit, 1);
    clazz.add_prototype_method("info", &client::info, 1);
    clazz.add_prototype_method("log", &client::log, 1);
    clazz.add_prototype_method("remove", &client::remove, 1);
    clazz.add_prototype_method("resolve", &client::resolve, 1);
    clazz.add_prototype_method("revert", &client::revert, 1);
    clazz.add_prototype_method("status", &client::status, 1);
    clazz.add_prototype_method("update", &client::update, 1);

    clazz.add_prototype_method("get_working_copy_root", &client::get_working_copy_root, 1);

    exports["Client"].set(clazz.get_constructor(), no::property_attribute::read_only);
}

std::shared_ptr<client> client::constructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = args.GetIsolate();

    std::optional<const std::string> config_path;
    if (args[0]->IsString()) {
        config_path.emplace(convert_string(args[0]));
    }

    return std::shared_ptr<client>(new client(isolate, config_path));
}

v8::Local<v8::Value> client::add_simple_auth_provider(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = args.GetIsolate();

    if (!args[0]->IsFunction()) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::Empty(isolate)));
        return v8::Local<v8::Value>();
    }

    _simple_auth_provider.add(args[0].As<v8::Function>());
    return v8::Local<v8::Value>();
}

v8::Local<v8::Value> client::remove_simple_auth_provider(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = args.GetIsolate();

    if (!args[0]->IsFunction()) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::Empty(isolate)));
        return v8::Local<v8::Value>();
    }

    _simple_auth_provider.remove(args[0].As<v8::Function>());
    return v8::Local<v8::Value>();
}

METHOD_BEGIN(add_to_changelist)
    auto paths      = convert_array(args[0], false);
    auto changelist = convert_string(args[1]);

    auto options     = convert_options(args[2]);
    auto depth       = convert_depth(isolate, options, "depth", svn::depth::infinity);
    auto changelists = convert_array(isolate, options, "changelists");

    ASYNC_BEGIN(paths, changelist, depth, changelists)
        _client->add_to_changelist(paths, changelist, depth, changelists);
    ASYNC_END()

    ASYNC_RESULT;
METHOD_RETURN(v8::Undefined(isolate))

v8::Local<v8::Value> client::get_changelists(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = args.GetIsolate();
    auto context = isolate->GetCurrentContext();

    auto path = convert_string(args[0]);

    auto options     = convert_options(args[1]);
    auto depth       = convert_depth(isolate, options, "depth", svn::depth::infinity);
    auto changelists = convert_array(isolate, options, "changelists");

    auto iterable = no::iterable::create(isolate, context);

    auto callback = [isolate, iterable](const char* path, const char* changelist) -> uv::future<void> {
        v8::HandleScope scope(isolate);

        no::object result(isolate);
        result["path"]       = path;
        result["changelist"] = changelist;

        return iterable->yield(result);
    };

    auto keep_alive = shared_from_this();
    auto work       = [this, keep_alive, path, callback, depth, changelists]() -> void {
        _client->get_changelists(path,
                                 uv::make_async(callback),
                                 depth,
                                 changelists);
    };

    auto after_work = [isolate, iterable](std::future<void> future) -> void {
        try {
            future.get();
            iterable->end();
        } catch (const svn::svn_error& raw) {
            v8::HandleScope scope(isolate);

            auto error = copy_error(isolate, raw);
            iterable->reject(error);
        }
    };

    uv::queue_work(work, after_work);

    return iterable->get();
}

METHOD_BEGIN(remove_from_changelists)
    auto paths = convert_array(args[0], false);

    auto options     = convert_options(args[1]);
    auto depth       = convert_depth(isolate, options, "depth", svn::depth::infinity);
    auto changelists = convert_array(isolate, options, "changelists");

    ASYNC_BEGIN(paths, depth, changelists)
        _client->remove_from_changelists(paths, depth, changelists);
    ASYNC_END()

    ASYNC_RESULT;
METHOD_RETURN(v8::Undefined(isolate))

METHOD_BEGIN(add)
    auto path = convert_string(args[0]);

    auto options = convert_options(args[1]);
    auto depth   = convert_depth(isolate, options, "depth", svn::depth::infinity);

    ASYNC_BEGIN(path, depth)
        _client->add(path, depth);
    ASYNC_END()

    ASYNC_RESULT;
METHOD_RETURN(v8::Undefined(isolate))

v8::Local<v8::Value> client::blame(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = args.GetIsolate();
    auto context = isolate->GetCurrentContext();

    auto path = convert_string(args[0]);

    auto options        = convert_options(args[1]);
    auto start_revision = convert_revision(isolate, options, "start_revision", svn::revision(0));
    auto end_revision   = convert_revision(isolate, options, "end_revision", svn::revision_kind::head);
    auto peg_revision   = convert_revision(isolate, options, "peg_revision", svn::revision_kind::unspecified);

    auto iterable = no::iterable::create(isolate, context);

    auto callback = [isolate, iterable](int32_t                start_revision,
                                        int32_t                end_revision,
                                        int64_t                line_number,
                                        std::optional<int32_t> revision,
                                        std::optional<int32_t> merged_revision,
                                        const char*            merged_path,
                                        const char*            line,
                                        bool                   local_change) -> uv::future<void> {
        v8::HandleScope scope(isolate);

        no::object result(isolate);
        result["start_revision"]  = start_revision;
        result["end_revision"]    = end_revision;
        result["line_number"]     = line_number;
        result["revision"]        = revision;
        result["merged_revision"] = merged_revision;
        result["merged_path"]     = merged_path;
        result["line"]            = line;
        result["local_change"]    = local_change;

        return iterable->yield(result);
    };

    auto keep_alive = shared_from_this();
    auto work       = [this, keep_alive, path, start_revision, end_revision, callback, peg_revision]() -> void {
        _client->blame(path,
                       start_revision,
                       end_revision,
                       uv::make_async(callback),
                       peg_revision,
                       svn::diff_ignore_space::none);
    };

    auto after_work = [isolate, iterable](std::future<void> future) -> void {
        try {
            future.get();
            iterable->end();
        } catch (const svn::svn_error& raw) {
            v8::HandleScope scope(isolate);

            auto error = copy_error(isolate, raw);
            iterable->reject(error);
        }
    };

    uv::queue_work(work, after_work);

    return iterable->get();
}

METHOD_BEGIN(cat)
    auto path = convert_string(args[0]);

    auto options      = convert_options(args[1]);
    auto peg_revision = convert_revision(isolate, options, "peg_revision", svn::revision_kind::unspecified);
    auto revision     = convert_revision(isolate, options, "revision", svn::revision_kind::unspecified);

    ASYNC_BEGIN(path, peg_revision, revision)
        return _client->cat(path, peg_revision, revision);
    ASYNC_END()

    auto raw_result = ASYNC_RESULT;

    no::object result(isolate);
    result["content"] = buffer_from_vector(isolate, raw_result.content);

    no::object properties(isolate);
    for (auto pair : raw_result.properties) {
        properties[pair.first] = pair.second;
    }
    result["properties"] = properties;
METHOD_RETURN(result)

METHOD_BEGIN(checkout)
    auto url  = convert_string(args[0]);
    auto path = convert_string(args[1]);

    auto options      = convert_options(args[2]);
    auto peg_revision = convert_revision(isolate, options, "peg_revision", svn::revision_kind::head);
    auto revision     = convert_revision(isolate, options, "revision", svn::revision_kind::head);
    auto depth        = convert_depth(isolate, options, "depth", svn::depth::infinity);

    ASYNC_BEGIN(url, path, peg_revision, revision, depth)
        return _client->checkout(url, path, peg_revision, revision, depth);
    ASYNC_END()

    auto result = ASYNC_RESULT;
METHOD_RETURN(no::data(isolate, result))

METHOD_BEGIN(cleanup)
    auto path = convert_string(args[0]);

    ASYNC_BEGIN(path)
        _client->cleanup(path, true, true, true, true, true);
    ASYNC_END()

    ASYNC_RESULT;
METHOD_RETURN(v8::Undefined(isolate))

static decltype(auto) convert_commit_callback(v8::Isolate* isolate, std::shared_ptr<no::iterable> iterable) {
    auto _callback = [isolate, iterable](const svn::commit_info& raw_commit) -> uv::future<void> {
        v8::HandleScope scope(isolate);

        no::object result(isolate);
        result["author"]            = raw_commit.author;
        result["date"]              = raw_commit.date;
        result["repos_root"]        = raw_commit.repos_root;
        result["revision"]          = raw_commit.revision;
        result["post_commit_error"] = raw_commit.post_commit_error;

        return iterable->yield(result);
    };

    return uv::make_async(_callback);
}

v8::Local<v8::Value> client::commit(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = args.GetIsolate();
    auto context = isolate->GetCurrentContext();

    auto paths   = convert_array(args[0], false);
    auto message = convert_string(args[1]);

    auto iterable = no::iterable::create(isolate, context);
    auto callback = convert_commit_callback(isolate, iterable);

    auto keep_alive = shared_from_this();
    auto work       = [this, keep_alive, paths, message, callback]() -> void {
        _client->commit(paths, message, callback);
    };

    auto after_work = [isolate, iterable](std::future<void> future) -> void {
        try {
            future.get();
            iterable->end();
        } catch (const svn::svn_error& raw) {
            v8::HandleScope scope(isolate);

            auto error = copy_error(isolate, raw);
            iterable->reject(error);
        }
    };

    uv::queue_work(work, after_work);

    return iterable->get();
}

static auto convert_to_date(v8::Local<v8::Context>& context, int64_t value) {
    auto d = static_cast<double>(value / 1000);
    return v8::Date::New(context, d).ToLocalChecked();
}

v8::Local<v8::Value> client::info(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = args.GetIsolate();
    auto context = isolate->GetCurrentContext();

    auto path = convert_string(args[0]);

    auto options      = convert_options(args[1]);
    auto peg_revision = convert_revision(isolate, options, "peg_revision", svn::revision_kind::unspecified);
    auto revision     = convert_revision(isolate, options, "revision", svn::revision_kind::unspecified);
    auto depth        = convert_depth(isolate, options, "depth", svn::depth::empty);

    auto iterable = no::iterable::create(isolate, context);

    auto callback = [isolate, iterable](const char* path, const svn::info& raw_info) -> uv::future<void> {
        v8::HandleScope scope(isolate);

        auto context = isolate->GetCurrentContext();

        no::object result(isolate);
        result["path"]                  = path;
        result["kind"]                  = static_cast<int32_t>(raw_info.kind);
        result["last_changed_author"]   = raw_info.last_changed_author;
        result["last_changed_date"]     = convert_to_date(context, raw_info.last_changed_date);
        result["last_changed_revision"] = static_cast<int32_t>(raw_info.last_changed_revision);
        result["repos_root_url"]        = raw_info.repos_root_url;
        result["repos_root_uuid"]       = raw_info.repos_uuid;
        result["url"]                   = raw_info.url;

        return iterable->yield(result);
    };

    auto keep_alive = shared_from_this();
    auto work       = [this, keep_alive, path, callback, peg_revision, revision, depth]() -> void {
        _client->info(path, uv::make_async(callback), peg_revision, revision, depth);
    };

    auto after_work = [isolate, iterable](std::future<void> future) -> void {
        try {
            future.get();
            iterable->end();
        } catch (const svn::svn_error& raw) {
            v8::HandleScope scope(isolate);

            auto error = copy_error(isolate, raw);
            iterable->reject(error);
        }
    };

    uv::queue_work(work, after_work);

    return iterable->get();
}

v8::Local<v8::Value> client::log(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = args.GetIsolate();
    auto context = isolate->GetCurrentContext();

    auto paths = convert_array(args[0], false);

    auto options         = convert_options(args[1]);
    auto peg_revision    = convert_revision(isolate, options, "peg_revision", svn::revision_kind::unspecified);
    auto revision_ranges = convert_revision_ranges(isolate, options, "revision_ranges");
    auto limit           = convert_number(isolate, options, "limit", 0);

    auto iterable = no::iterable::create(isolate, context);

    auto callback = [isolate, iterable](svn::log_entry& entry) -> uv::future<void> {
        v8::HandleScope scope(isolate);

        no::object result(isolate);
        result["revision"]          = entry.revision;
        result["non_inheritable"]   = entry.non_inheritable;
        result["subtractive_merge"] = entry.subtractive_merge;
        result["author"]            = entry.author;
        result["date"]              = entry.date;
        result["message"]           = entry.message;

        return iterable->yield(result);
    };

    auto keep_alive = shared_from_this();
    auto work       = [this, keep_alive, paths, callback, revision_ranges, limit, peg_revision]() -> void {
        _client->log(paths, uv::make_async(callback), revision_ranges, limit, peg_revision, false, false, false, {});
    };

    auto after_work = [isolate, iterable](std::future<void> future) -> void {
        try {
            future.get();
            iterable->end();
        } catch (const svn::svn_error& raw) {
            v8::HandleScope scope(isolate);

            auto error = copy_error(isolate, raw);
            iterable->reject(error);
        }
    };

    uv::queue_work(work, after_work);

    return iterable->get();
}

v8::Local<v8::Value> client::remove(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = args.GetIsolate();
    auto context = isolate->GetCurrentContext();

    auto paths = convert_array(args[0], false);

    auto iterable = no::iterable::create(isolate, context);
    auto callback = convert_commit_callback(isolate, iterable);

    auto keep_alive = shared_from_this();
    auto work       = [this, keep_alive, paths, callback]() -> void {
        _client->remove(paths, callback);
    };

    auto after_work = [isolate, iterable](std::future<void> future) -> void {
        try {
            future.get();
            iterable->end();
        } catch (const svn::svn_error& raw) {
            v8::HandleScope scope(isolate);

            auto error = copy_error(isolate, raw);
            iterable->reject(error);
        }
    };

    uv::queue_work(work, after_work);

    return iterable->get();
}

METHOD_BEGIN(resolve)
    auto path = convert_string(args[0]);

    ASYNC_BEGIN(path)
        _client->resolve(path);
    ASYNC_END()

    ASYNC_RESULT;
METHOD_RETURN(v8::Undefined(isolate));

METHOD_BEGIN(revert)
    auto paths = convert_array(args[0], false);

    ASYNC_BEGIN(paths)
        _client->revert(paths);
    ASYNC_END()

    ASYNC_RESULT;
METHOD_RETURN(v8::Undefined(isolate));

v8::Local<v8::Value> client::status(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = args.GetIsolate();
    auto context = isolate->GetCurrentContext();

    auto path = convert_string(args[0]);

    auto options          = convert_options(args[1]);
    auto revision         = convert_revision(isolate, options, "revision", svn::revision_kind::working);
    auto depth            = convert_depth(isolate, options, "depth", svn::depth::infinity);
    auto ignore_externals = convert_boolean(isolate, options, "ignore_externals", false);

    auto iterable = no::iterable::create(isolate, context);
    auto callback = [isolate, iterable](const std::string& path, const svn::status& raw_status) -> uv::future<void> {
        v8::HandleScope scope(isolate);

        auto context = isolate->GetCurrentContext();

        no::object result(isolate);
        result["path"]           = path;
        result["changelist"]     = raw_status.changelist;
        result["changed_author"] = raw_status.changed_author;
        result["changed_date"]   = convert_to_date(context, raw_status.changed_date);
        result["changed_rev"]    = raw_status.changed_rev;
        result["conflicted"]     = raw_status.conflicted;
        result["copied"]         = raw_status.copied;
        result["depth"]          = static_cast<int32_t>(raw_status.node_depth);
        result["file_external"]  = raw_status.file_external;
        result["kind"]           = static_cast<int32_t>(raw_status.kind);
        result["node_status"]    = static_cast<int32_t>(raw_status.node_status);
        result["prop_status"]    = static_cast<int32_t>(raw_status.prop_status);
        result["revision"]       = static_cast<int32_t>(raw_status.revision);
        result["text_status"]    = static_cast<int32_t>(raw_status.text_status);
        result["versioned"]      = raw_status.versioned;

        return iterable->yield(result);
    };

    auto keep_alive = shared_from_this();
    auto work       = [this, keep_alive, path, callback, revision, depth, ignore_externals]() -> void {
        _client->status(path, uv::make_async(callback), revision, depth, false, false, true, false, ignore_externals);
    };

    auto after_work = [isolate, iterable](std::future<void> future) -> void {
        try {
            future.get();
            iterable->end();
        } catch (const svn::svn_error& raw) {
            v8::HandleScope scope(isolate);

            auto error = copy_error(isolate, raw);
            iterable->reject(error);
        }
    };

    uv::queue_work(work, after_work);

    return iterable->get();
}

METHOD_BEGIN(update)
    auto paths  = convert_array(args[0], false);
    auto single = args[0]->IsString();

    ASYNC_BEGIN(paths)
        return _client->update(paths);
    ASYNC_END(single)

    v8::Local<v8::Value> result;
    if (single) {
        result = no::data(isolate, ASYNC_RESULT[0]);
    } else {
        auto vector = ASYNC_RESULT;
        auto array  = no::data<v8::Array>(isolate, static_cast<int32_t>(vector.size()));
        for (uint32_t i = 0; i < vector.size(); i++) {
            array->Set(i, no::data(isolate, vector[i]));
        }
        result = array;
    }
METHOD_RETURN(result);

METHOD_BEGIN(get_working_copy_root)
    auto path = convert_string(args[0]);

    ASYNC_BEGIN(path)
        return _client->get_working_copy_root(path);
    ASYNC_END()

    auto result = no::data(isolate, ASYNC_RESULT);
METHOD_RETURN(result);

client::client(v8::Isolate*                            isolate,
               const std::optional<const std::string>& config_path)
    : _client(new svn::client(config_path))
    , _simple_auth_provider(isolate) {
    _client->add_simple_auth_provider(std::make_shared<svn::client::simple_auth_provider::element_type>(std::ref(_simple_auth_provider)));
}
} // namespace no
