use spidermonkey;
import spidermonkey::js;
import ctypes::size_t;

fn main() {
    let rt = js::new_runtime(8u32 * 1024u32 * 1024u32);
    let cx = js::new_context(rt, 8192 as size_t);
    js::set_options(cx, js::options::varobjfix | js::options::methodjit);
    js::set_version(cx, 185u);

    let class = js::new_class({ name: "global", flags: 0x47700u32 });
    let global = js::new_compartment_and_global_object(cx, class,
                                                       js::null_principals());

    js::init_standard_classes(cx, global);

    let src = "\"Hello \" + \"world!\"";
    let script = js::compile_script(cx, global, str::bytes(src), "test.js",
                                    0u);
    let result = option::get(js::execute_script(cx, global, script));
    let result_src = js::value_to_source(cx, result);
    #error["Result: %s", js::get_string(cx, result_src)];
}

