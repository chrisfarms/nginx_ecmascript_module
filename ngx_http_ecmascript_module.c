// 
// module 

#define XP_UNIX // required for spidermonkey

#include <stdlib.h>
#include <string.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "jsapi.h"

static char* ngx_http_set_ecmascript_handler(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

// typedef struct {
// 	JSRuntime *rt;
// } ngx_http_ecmascript_loc_conf_t;

static ngx_command_t  ngx_http_ecmascript_commands[] = {
	{ ngx_string("ecmascript"),
    	NGX_HTTP_LOC_CONF|NGX_HTTP_LMT_CONF|NGX_CONF_TAKE1,
    	ngx_http_set_ecmascript_handler,
    	NGX_HTTP_LOC_CONF_OFFSET,
    	0,
    	NULL },
   ngx_null_command
};


static ngx_http_module_t ngx_http_ecmascript_module_ctx = {
    NULL,                                			// preconfiguration
    NULL,                                			// postconfiguration
    NULL,                                			// create main configuration
    NULL,                                			// init main configuration
    NULL,                                			// create server configuration
    NULL,                                			// merge server configuration
    NULL,																      // create location configuration
    NULL       															  // merge location configuration
};


ngx_module_t  ngx_http_ecmascript_module = {
    NGX_MODULE_V1,
    &ngx_http_ecmascript_module_ctx, 					// module context
    ngx_http_ecmascript_commands,   					// module directives
    NGX_HTTP_MODULE,               						// module type
    NULL,                          						// init master
    NULL,                          						// init module
    NULL,                          						// init process
    NULL,                          						// init thread
    NULL,                          						// exit thread
    NULL,         // exit process
    NULL,                          						// exit master
    NGX_MODULE_V1_PADDING
};

/* The class of the global object. */
static JSClass global_class = {
    "global", JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, JS_FinalizeStub,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

/* The JS error reporter callback. */
void reportError(JSContext *cx, const char *message, JSErrorReport *report)
{
    fprintf(stderr, "%s:%u:%s\n",
            report->filename ? report->filename : "<no filename>",
            (unsigned int) report->lineno,
            message);
}

JSBool js_land_rand(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    return JS_NewNumberValue(cx, rand(), rval);
}

/* A wrapper for the system() function, from the C standard library.
   This example shows how to report errors. */
JSBool js_land_system(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    const char *cmd;
    int rc;

    if (!JS_ConvertArguments(cx, argc, argv, "s", &cmd))
        return JS_FALSE;

    rc = system(cmd);
    if (rc != 0) {
        /* Throw a JavaScript exception. */
        JS_ReportError(cx, "Command failed with exit code %d", rc);
        return JS_FALSE;
    }

    *rval = JSVAL_VOID;  /* return undefined */
    return JS_TRUE;
}

JSBool js_land_require(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    const char *filename;
	JSScript *script;
	
    if (!JS_ConvertArguments(cx, argc, argv, "s", &filename)){
        return JS_FALSE;	
	}

	script = JS_CompileFile(cx, JS_GetGlobalObject(cx), filename);
	if(script == NULL){
        JS_ReportError(cx, "Could not compile %s", *filename);
		return JS_FALSE;		
	}
	
	if ( !JS_ExecuteScript(cx, JS_GetGlobalObject(cx), script, rval) ) {
        JS_ReportError(cx, "Could not evaluate script %s", *filename);
		return JS_FALSE;
	}

    return JS_TRUE;
}
/*
 *
 * globals
 *
 */
static JSFunctionSpec js_land_env[] = {
    {"rand", 		js_land_rand,    	0,0,0},
    {"system", 		js_land_system,  	1,0,0},
    {"require", 	js_land_require, 	1,0,0},
    {NULL,NULL,0,0,0}
};


static ngx_int_t ngx_http_ecmascript_handler(ngx_http_request_t *r)
{
    ngx_int_t     rc;
    ngx_buf_t    *b;
    ngx_chain_t   out;

	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Starting ECMAScript module handler");
		
    // ngx_http_ecmascript_loc_conf_t  *cglcf;
    // cglcf = ngx_http_get_module_loc_conf(r, ngx_http_ecmascript_module);

    // digit = (char *)r->uri.data + r->uri.len - 1;

	// setup headers
    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";
    r->headers_out.status = NGX_HTTP_OK;


	// STARTJS
    /* JS variables. */
	JSRuntime *rt;
    JSContext *cx;
    JSObject  *global;

   	rt = JS_NewRuntime(8L * 1024L * 1024L);
    if (rt == NULL){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to create JS Runtime.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
		
    /* Create a context. */
    cx = JS_NewContext(rt, 8192);
    if (cx == NULL){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to create JS Context.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
    JS_SetOptions(cx, JSOPTION_VAROBJFIX);
    JS_SetErrorReporter(cx, reportError);

    /* Create the global object. */
    global = JS_NewObject(cx, &global_class, NULL, NULL);
    if (global == NULL){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to create JS global object.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

    /* Populate the global object with the standard globals */
    if (!JS_InitStandardClasses(cx, global)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to populate JS land with standard globals.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	
	if (!JS_DefineFunctions(cx, global, js_land_env)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to populate JS land with custom globals");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	
	JSObject *request_obj = JS_NewObject(cx, NULL, NULL, NULL);
	if (request_obj == NULL){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Could not create request object for some reason");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;		
	}
	if(!JS_DefineProperty(cx, request_obj, "url", STRING_TO_JSVAL(JS_NewStringCopyN(cx, "TEST", strlen("TEST"))), NULL, NULL, JSPROP_ENUMERATE)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "prop def failed!");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	// JS_SetProperty(cx, global, "url", &strval);

	JSScript *env;
	env = JS_CompileFile(cx, global, "/Users/chrisfarms/src/nginx_ecmascript/env.js");
	if(env == NULL){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Could not open env.js script");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;		
	}

	// JSObject = *envObj;
	// envObj = JS_NewScriptObject(cx, env);
	//     if (envObj == NULL) {
	// 	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Could not create env object wrapper");
	// 	return NGX_HTTP_INTERNAL_SERVER_ERROR;
	//     }
	// 
	// if (!JS_AddNamedRoot(cx, &envObj, "env")){
	// 	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Promlem creating named GC root for env");
	// 	return NGX_HTTP_INTERNAL_SERVER_ERROR;
	// }
	
	jsval result;	
	if ( !JS_ExecuteScript(cx, global, env, &result) ) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Could not evaluate script");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	jsval argv[1];
	argv[0] = OBJECT_TO_JSVAL(request_obj);
	if( !JS_CallFunctionName(cx, global, "process", 1, argv, &result) ){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Could not call 'process' function");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	
	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "converting...");
	JSString *str = JS_ValueToString(cx, result);
	char *bufbytes;
	bufbytes = JS_GetStringBytes(str);
	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "assigning to buff...");
	// setup buffer
	b = ngx_create_temp_buf(r->pool, strlen(bufbytes));
    if (b == NULL) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate response buffer.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    out.buf = b;
    out.next = NULL;
	b->last = ngx_sprintf(b->last, "%s", bufbytes);
	
    /* Cleanup. */
	// JS_RemoveRoot(cx, &envObj);
	// JS_MaybeGC(cx);
	JS_DestroyScript(cx, env);
    JS_DestroyContext(cx);
    JS_DestroyRuntime(rt);
    JS_ShutDown();
	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "done shutdown");


	r->headers_out.content_length_n = b->last - b->pos;

    b->last_buf = 1;

	// return headers
    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "No body");
        return rc;
    }
	// return body
    return ngx_http_output_filter(r, &out);
}

static char * ngx_http_set_ecmascript_handler(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;
    // ngx_http_ecmascript_loc_conf_t *cglcf = conf;
				
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_ecmascript_handler;

    return NGX_CONF_OK;
}

