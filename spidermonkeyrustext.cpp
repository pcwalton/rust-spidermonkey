typedef void (*JSPropertyOp)();
typedef void (*JSStrictPropertyOp)();
typedef void (*JSEnumerateOp)();
typedef void (*JSResolveOp)();
typedef void (*JSConvertOp)();
typedef void (*JSFinalizeOp)();

extern "C" void JS_PropertyStub();
extern "C" void JS_StrictPropertyStub();
extern "C" void JS_EnumerateStub();
extern "C" void JS_ResolveStub();
extern "C" void JS_ConvertStub();
extern "C" void JS_FinalizeStub();

extern "C" {

JSPropertyOp JSRust_GetPropertyStub() {
    return JS_PropertyStub;
}

JSStrictPropertyOp JSRust_GetStrictPropertyStub() {
    return JS_StrictPropertyStub;
}

JSEnumerateOp JSRust_GetEnumerateStub() {
    return JS_EnumerateStub;
}

JSResolveOp JSRust_GetResolveStub() {
    return JS_ResolveStub;
}

JSConvertOp JSRust_GetConvertStub() {
    return JS_ConvertStub;
}

JSFinalizeOp JSRust_GetFinalizeStub() {
    return JS_FinalizeStub;
}

}


