#include <js/jsapi.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdint.h>

/*
 * Rust API declarations.
 *
 * TODO: Rust should expose a nice header file for this kind of thing.
 */

typedef uintptr_t rust_port_id;
typedef uintptr_t rust_task_id;
struct rust_port;
struct type_desc;

struct rust_chan_pkg {
    rust_task_id task;
    rust_port_id port;

    rust_chan_pkg() : task(0), port(0) {}
};

struct rust_s_shared_malloc_args {
    uintptr_t retval;
    size_t nbytes;
    type_desc *td;
};

extern "C" rust_port *new_port(size_t unit_sz);
extern "C" void del_port(rust_port *port);
extern "C" uintptr_t chan_id_send(const type_desc *t,
                                  rust_task_id target_task_id,
                                  rust_port_id target_port_id, void *sptr);
extern "C" void upcall_s_shared_malloc(rust_s_shared_malloc_args *args);

class rust_str {
private:
    uintptr_t size;
    uintptr_t pad;
    char data[0];

    rust_str() { /* Don't call me. */ }

public:
    static rust_str *make(const char *c_str) {
        uintptr_t len = strlen(c_str);
        size_t obj_len = sizeof(rust_str) + len + 1;
        rust_s_shared_malloc_args args = { 0, obj_len, NULL };
        upcall_s_shared_malloc(&args);

        rust_str *str = reinterpret_cast<rust_str *>(args.retval);
        str->size = len;
        strcpy(str->data, c_str);
        return str;
    }
};

/*
 * SpiderMonkey helpers, needed since Rust doesn't support C++ global
 * variables.
 */

extern "C" JSPropertyOp JSRust_GetPropertyStub() {
    return JS_PropertyStub;
}

extern "C" JSStrictPropertyOp JSRust_GetStrictPropertyStub() {
    return JS_StrictPropertyStub;
}

extern "C" JSEnumerateOp JSRust_GetEnumerateStub() {
    return JS_EnumerateStub;
}

extern "C" JSResolveOp JSRust_GetResolveStub() {
    return JS_ResolveStub;
}

extern "C" JSConvertOp JSRust_GetConvertStub() {
    return JS_ConvertStub;
}

extern "C" JSFinalizeOp JSRust_GetFinalizeStub() {
    return JS_FinalizeStub;
}

/* Port and channel constructors */

namespace {

struct jsrust_context_priv {
    const type_desc *error_tydesc;
    rust_chan_pkg error_chan;

    const type_desc *log_tydesc;
    rust_chan_pkg log_chan;

    jsrust_context_priv() : error_tydesc(NULL), error_chan() {}
};

struct jsrust_error_report {
    rust_str *message;
    rust_str *filename;
    uintptr_t lineno;
    uintptr_t flags;
};

struct jsrust_log_message {
    rust_str *message;
    uintptr_t level;
};

void port_finalize(JSContext *cx, JSObject *obj) {
    rust_port *port = reinterpret_cast<rust_port *>(JS_GetPrivate(cx, obj));
    if (port)
        del_port(port);
}

JSClass port_class = {
    "Port",                         /* name */
    JSCLASS_HAS_PRIVATE,            /* flags */
    JS_PropertyStub,                /* addProperty */
    JS_PropertyStub,                /* delProperty */
    JS_PropertyStub,                /* getProperty */
    JS_StrictPropertyStub,          /* setProperty */
    JS_EnumerateStub,               /* enumerate */
    JS_ResolveStub,                 /* resolve */
    JS_ConvertStub,                 /* convert */
    port_finalize,                  /* finalize */
    JSCLASS_NO_OPTIONAL_MEMBERS
};

JSBool jsrust_new_port(JSContext *cx, uintN argc, jsval *vp) {
    jsval constructor = JS_THIS(cx, vp);
    JSObject *obj = JS_NewObject(
        cx, &port_class, NULL, JSVAL_TO_OBJECT(constructor));

    if (!obj) {
        JS_ReportError(cx, "Could not create Port");
        return JS_FALSE;
    }

    rust_port *port = new_port(sizeof(void *) * 2);
    JS_SetPrivate(cx, obj, port);
    JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(obj));
    return JS_TRUE;
}

JSBool jsrust_port_channel(JSContext *cx, uintN argc, jsval *vp) {
    jsval self = JS_THIS(cx, vp);
    rust_port *port = (rust_port *)JS_GetPrivate(cx, JSVAL_TO_OBJECT(self));
    // todo make channel and return it
    JS_SET_RVAL(cx, vp, JSVAL_NULL);
    return JS_TRUE;
}

static JSFunctionSpec port_functions[] = {
    JS_FN("channel", jsrust_port_channel, 0, 0),
    JS_FS_END
};

void jsrust_report_error(JSContext *cx, const char *c_message,
                         JSErrorReport *c_report)
{
    void *priv_p = JS_GetContextPrivate(cx);
    assert(priv_p && "No private data associated with context!");
    jsrust_context_priv *priv =
        reinterpret_cast<jsrust_context_priv *>(priv_p);

    rust_str *message = rust_str::make(c_message);
    rust_str *filename = rust_str::make(c_report->filename);
    jsrust_error_report report =
        { message, filename, c_report->lineno, c_report->flags };

    chan_id_send(priv->error_tydesc, priv->error_chan.task,
                 priv->error_chan.port, &report);

    jsrust_log_message log_report =
        { message, 1 };

    chan_id_send(priv->log_tydesc, priv->log_chan.task,
                 priv->log_chan.port, &log_report);

}

}   /* end anonymous namespace */

extern "C" JSContext *JSRust_NewContext(JSRuntime *rt, size_t size) {
    JSContext *cx = JS_NewContext(rt, size);
    if (!cx)
        return NULL;

    jsrust_context_priv *priv = new jsrust_context_priv();
    JS_SetContextPrivate(cx, priv);
    return cx;
}

extern "C" JSBool JSRust_InitRustLibrary(JSContext *cx, JSObject *global) {
    JSObject *result = JS_InitClass(
        cx, global, NULL,
        &port_class,
        jsrust_new_port,
        0, // 0 args
        NULL, // no properties
        port_functions, // no functions
        NULL, NULL);

    return !!result;
}

extern "C" JSBool JSRust_SetErrorChannel(JSContext *cx,
                                         const rust_chan_pkg *channel,
                                         const type_desc *tydesc) {
    void *priv_p = JS_GetContextPrivate(cx);
    assert(priv_p && "No private data associated with context!");
    jsrust_context_priv *priv =
        reinterpret_cast<jsrust_context_priv *>(priv_p);

    priv->error_tydesc = tydesc;
    priv->error_chan = *channel;

    JS_SetErrorReporter(cx, jsrust_report_error);
    return JS_TRUE;
}

JSBool JSRustPostMessage(JSContext *cx, uintN argc, jsval *vp) {
    void *priv_p = JS_GetContextPrivate(cx);
    assert(priv_p && "No private data associated with context!");
    jsrust_context_priv *priv =
        reinterpret_cast<jsrust_context_priv *>(priv_p);

    JSString * msg;
    JS_ConvertArguments(cx,
        1, JS_ARGV(cx, vp), "S", &msg);

    const char *code = JS_EncodeString(cx, msg);
    rust_str *message = rust_str::make(code);

    jsrust_log_message report =
        { message, 0 };

    chan_id_send(priv->log_tydesc, priv->log_chan.task,
                 priv->log_chan.port, &report);

    JS_SET_RVAL(cx, vp, JSVAL_NULL);
    return JS_TRUE;
}

static JSFunctionSpec print_functions[] = {
    JS_FN("print", JSRustPostMessage, 0, 0),
    JS_FS_END
};

extern "C" JSBool JSRust_SetLogChannel(JSContext *cx,
                                         JSObject *global,
                                         const rust_chan_pkg *channel,
                                         const type_desc *tydesc) {
    void *priv_p = JS_GetContextPrivate(cx);
    assert(priv_p && "No private data associated with context!");
    jsrust_context_priv *priv =
        reinterpret_cast<jsrust_context_priv *>(priv_p);

    priv->log_tydesc = tydesc;
    priv->log_chan = *channel;

    JS_DefineFunctions(cx, global, print_functions);

    return JS_TRUE;
}

