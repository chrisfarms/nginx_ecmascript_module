#define XP_UNIX // required for spidermonkey

#include <string.h>
#include <js/jsapi.h>

/* The class of the global object. */
static JSClass global_class = {
    "global", JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, JS_FinalizeStub,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

/* The error reporter callback. */
void reportError(JSContext *cx, const char *message, JSErrorReport *report)
{
    fprintf(stderr, "%s:%u:%s\n",
            report->filename ? report->filename : "<no filename>",
            (unsigned int) report->lineno,
            message);
}

int main(int argc, const char *argv[])
{
    /* JS variables. */
    JSRuntime *rt;
    JSContext *cx;
    JSObject  *global;

    /* Create a JS runtime. */
    rt = JS_NewRuntime(8L * 1024L * 1024L);
    if (rt == NULL)
        return 1;

    /* Create a context. */
    cx = JS_NewContext(rt, 8192);
    if (cx == NULL)
        return 1;
    JS_SetOptions(cx, JSOPTION_VAROBJFIX);
    // JS_SetVersion(cx, JSVERSION_LATEST);
    JS_SetErrorReporter(cx, reportError);

    /* Create the global object. */
    global = JS_NewObject(cx, &global_class, NULL, NULL);
    if (global == NULL)
        return 1;

    /* Populate the global object with the standard globals,
       like Object and Array. */
    if (!JS_InitStandardClasses(cx, global))
        return 1;


		printf("spidermonkey test\n");
		// These should indicate source location for diagnostics.
		char *filename = "system-environment";
		uintN lineno = 0;
		/*
		 * The return value comes back here -- if it could be a GC thing, you must
		 * add it to the GC's "root set" with JS_AddRoot(cx, &thing) where thing
		 * is a JSString *, JSObject *, or jsdouble *, and remove the root before
		 * rval goes out of scope, or when rval is no longer needed.
		 */
		jsval rval;
		JSBool ok;
		/*
		 * Some example source in a C string.  Larger, non-null-terminated buffers
		 * can be used, if you pass the buffer length to JS_EvaluateScript.
		 */
		char *source = "1 * 10";
		ok = JS_EvaluateScript(cx, global, source, strlen(source), filename, lineno, &rval);
		/* Should get a number back from the example source. */
		if (ok) {
		    jsdouble d;
		    ok = JS_ValueToNumber(cx, rval, &d);
				printf("result:%g\n",d);
		}
    /* Cleanup. */
    JS_DestroyContext(cx);
    JS_DestroyRuntime(rt);
    JS_ShutDown();
    return 0;
}