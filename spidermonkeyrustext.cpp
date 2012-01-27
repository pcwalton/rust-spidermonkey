#include <js/jsapi.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <pthread.h>

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

    const type_desc *io_tydesc;
    rust_chan_pkg io_chan;

    JSObject *set_timeout_cbs;

    jsrust_context_priv() : error_tydesc(NULL), error_chan(), log_tydesc(NULL), log_chan(), io_tydesc(NULL), io_chan(), set_timeout_cbs(NULL) {}
};

struct jsrust_error_report {
    rust_str *message;
    rust_str *filename;
    uintptr_t lineno;
    uintptr_t flags;
};

struct jsrust_log_message {
    rust_str *message;
    uint32_t level;
};

struct jsrust_io_event {
    uint32_t a1;
    rust_str *a2;
    uint32_t a3;
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

// stolen from js shell
static JSBool JSRust_Print(JSContext *cx, uintN argc, jsval *vp) {
    jsval *argv;
    uintN i;
    JSString *str;
    char *bytes;

    printf("%p ", pthread_self());

    argv = JS_ARGV(cx, vp);
    for (i = 0; i < argc; i++) {
        str = JS_ValueToString(cx, argv[i]);
        if (!str)
            return JS_FALSE;
        bytes = JS_EncodeString(cx, str);
        if (!bytes)
            return JS_FALSE;
        printf("%s%s", i ? " " : "", bytes);
        JS_free(cx, bytes);
    }
    printf("\n");
    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSFunctionSpec global_functions[] = {
    JS_FN("print", JSRust_Print, 0, 0),
    JS_FS_END
};

extern "C" JSBool JSRust_InitRustLibrary(JSContext *cx, JSObject *global) {
    JSObject *result = JS_InitClass(
        cx, global, NULL,
        &port_class,
        jsrust_new_port,
        0, // 0 args
        NULL, // no properties
        port_functions, // no functions
        NULL, NULL);

    JS_DefineFunctions(cx, global, global_functions);

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

JSBool JSRust_PostMessage(JSContext *cx, uintN argc, jsval *vp) {
    void *priv_p = JS_GetContextPrivate(cx);
    assert(priv_p && "No private data associated with context!");
    jsrust_context_priv *priv =
        reinterpret_cast<jsrust_context_priv *>(priv_p);

    JSString *thestr = JS_ValueToSource(cx, JS_ARGV(cx, vp)[0]);
    const char *code = JS_EncodeString(cx, thestr);
    rust_str *message = rust_str::make(code);

    jsrust_log_message report =
        { message, 0 };

    chan_id_send(priv->log_tydesc, priv->log_chan.task,
                 priv->log_chan.port, &report);

    JS_SET_RVAL(cx, vp, JSVAL_NULL);
    return JS_TRUE;
}

static JSFunctionSpec postMessage_functions[] = {
    JS_FN("postMessage", JSRust_PostMessage, 0, 0),
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

    JS_DefineFunctions(cx, global, postMessage_functions);

    return JS_TRUE;
}

extern "C" JSBool JSRust_Exit(int code) {
    exit(code);
}

static uint32_t timeout_num = 0;

enum IO_OP {
    CONNECT,
    SEND,
    RECV,
    CLOSE
};

JSBool JSRust_Connect(JSContext *cx, uintN argc, jsval *vp) {
    void *priv_p = JS_GetContextPrivate(cx);
    assert(priv_p && "No private data associated with context!");
    jsrust_context_priv *priv =
        reinterpret_cast<jsrust_context_priv *>(priv_p);

    JSString *a2str;

    JS_ConvertArguments(cx,
        1, JS_ARGV(cx, vp), "S", &a2str);

    rust_str *a2 = rust_str::make(
        JS_EncodeString(cx, a2str));

    uint32_t my_num = timeout_num++;

    jsrust_io_event evt = { CONNECT, a2, my_num };

    chan_id_send(priv->io_tydesc, priv->io_chan.task,
                 priv->io_chan.port, &evt);

    JS_SET_RVAL(cx, vp, INT_TO_JSVAL(my_num));
    return JS_TRUE;
}

JSBool JSRust_Send(JSContext *cx, uintN argc, jsval *vp) {
    void *priv_p = JS_GetContextPrivate(cx);
    assert(priv_p && "No private data associated with context!");
    jsrust_context_priv *priv =
        reinterpret_cast<jsrust_context_priv *>(priv_p);

    uint32_t req_id;
    JSString *data;

    JS_ConvertArguments(cx,
        2, JS_ARGV(cx, vp), "uS", &req_id, &data);

    rust_str *data_rust = rust_str::make(
        JS_EncodeString(cx, data));

    jsrust_io_event evt = { SEND, data_rust, req_id };

    chan_id_send(priv->io_tydesc, priv->io_chan.task,
                 priv->io_chan.port, &evt);

    JS_SET_RVAL(cx, vp, JSVAL_NULL);
    return JS_TRUE;
}

JSBool JSRust_Recv(JSContext *cx, uintN argc, jsval *vp) {
    void *priv_p = JS_GetContextPrivate(cx);
    assert(priv_p && "No private data associated with context!");
    jsrust_context_priv *priv =
        reinterpret_cast<jsrust_context_priv *>(priv_p);

    uint32_t req_id;
    JSString *amount_str;

    JS_ConvertArguments(cx,
        2, JS_ARGV(cx, vp), "uS", &req_id, &amount_str);

    rust_str *amount_rust = rust_str::make(
        JS_EncodeString(cx, amount_str));

    jsrust_io_event evt = { RECV, amount_rust, req_id };

    chan_id_send(priv->io_tydesc, priv->io_chan.task,
                 priv->io_chan.port, &evt);

    JS_SET_RVAL(cx, vp, JSVAL_NULL);
    return JS_TRUE;
}

JSBool JSRust_Close(JSContext *cx, uintN argc, jsval *vp) {
    void *priv_p = JS_GetContextPrivate(cx);
    assert(priv_p && "No private data associated with context!");
    jsrust_context_priv *priv =
        reinterpret_cast<jsrust_context_priv *>(priv_p);

    uint32_t req_id;

    JS_ConvertArguments(cx,
        1, JS_ARGV(cx, vp), "u", &req_id);

    rust_str *nothing = rust_str::make("");

    jsrust_io_event evt = { SEND, nothing, req_id };

    chan_id_send(priv->io_tydesc, priv->io_chan.task,
                 priv->io_chan.port, &evt);

    JS_SET_RVAL(cx, vp, JSVAL_NULL);
    return JS_TRUE;
}

static JSFunctionSpec io_functions[] = {
    JS_FN("jsrust_connect", JSRust_Connect, 1, 0),
    JS_FN("jsrust_send", JSRust_Send, 2, 0),
    JS_FN("jsrust_recv", JSRust_Recv, 2, 0),
    JS_FN("jsrust_close", JSRust_Close, 1, 0),
    JS_FS_END
};

extern "C" JSBool JSRust_SetIoChannel(JSContext *cx,
                                         JSObject *global,
                                         const rust_chan_pkg *channel,
                                         const type_desc *tydesc) {
    void *priv_p = JS_GetContextPrivate(cx);
    assert(priv_p && "No private data associated with context!");
    jsrust_context_priv *priv =
        reinterpret_cast<jsrust_context_priv *>(priv_p);

    priv->io_tydesc = tydesc;
    priv->io_chan = *channel;

    JS_DefineFunctions(cx, global, io_functions);
    JS_DefineObject(cx, global, "_io_callbacks", NULL, NULL, 0);

    return JS_TRUE;
}

extern "C" void JSRust_SetDataOnObject(JSContext *cx, JSObject *obj, const char *val, uint32_t vallen) {
    JSString *valstr = JS_NewStringCopyN(cx, val, vallen);
    jsval *jv = (jsval *)malloc(sizeof(jsval));
    *jv = STRING_TO_JSVAL(valstr);
    JS_SetProperty(cx, obj, "_data", jv);
}

static pthread_mutex_t get_runtime_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t thread_runtime_key;
static int initialized = 0;

JSRuntime *jsrust_getthreadruntime(uint32_t max_bytes) {
    JSRuntime *rt;
    pthread_mutex_lock(&get_runtime_mutex);

    if (!initialized) {
        pthread_key_create(&thread_runtime_key, NULL);
        initialized = 1;
    }
    pthread_mutex_unlock(&get_runtime_mutex);

    rt = (JSRuntime *)pthread_getspecific(thread_runtime_key);
    if (rt == NULL) {
        rt = JS_NewRuntime(max_bytes);
        pthread_setspecific(thread_runtime_key, (const void *)rt);
    }
    return rt;
}

extern "C" JSRuntime *JSRust_GetThreadRuntime(uint32_t max_bytes) {
    return jsrust_getthreadruntime(max_bytes);
}

