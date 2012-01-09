#include <js/jsapi.h>
#include <cstdlib>

/*
 * Rust API declarations.
 *
 * TODO: Rust should expose a nice header file for this kind of thing.
 */

struct rust_port;

extern "C" rust_port *new_port(size_t unit_sz);
extern "C" void del_port(rust_port *port);

/* SpiderMonkey helpers, needed since Rust doesn't support C++ global variables. */

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

void port_finalize(JSContext *cx, JSObject *obj) {
    /*rust_port *port = reinterpret_cast<rust_port *>(JS_GetPrivate(cx, obj));
    if (port)
        del_port(port);*/
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
    JS_FinalizeStub,                  /* finalize */
    JSCLASS_NO_OPTIONAL_MEMBERS
};

JSBool jsrust_new_port(JSContext *cx, uintN argc, jsval *vp) {
    JSObject *obj = JS_THIS_OBJECT(cx, vp);
    if (!obj) {
        JS_ReportError(cx, "|this| is not an object");
        return JS_FALSE;
    }

    rust_port *port = new_port(sizeof(void *) * 2);
    JS_SetPrivate(cx, obj, port);
    JS_SET_RVAL(cx, vp, JS_THIS(cx, vp));
    return JS_TRUE;
}

}   /* end anonymous namespace */

extern "C" JSBool JSRust_InitRustLibrary(JSContext *cx, JSObject *obj) {
    JSFunction *fn = JS_DefineFunction(cx, obj, "Port", jsrust_new_port, 0, 0);
    return !!fn;
}

