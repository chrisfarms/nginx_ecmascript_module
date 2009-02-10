// 
// module 

#define XP_UNIX 				// required for spidermonkey
// #define NGX_HTTP_HEADERS 1 		// enable nginx Accept header parsing

#include <stdlib.h>
#include <string.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <js/jsapi.h>

#include "http.h"

static char* ngx_http_ecmascript_handler_setup(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void* ngx_http_ecma_create_main_conf(ngx_conf_t *cf);
static void* ngx_http_ecma_create_loc_conf(ngx_conf_t *cf);

typedef struct {
	JSRuntime *runtime;
} ngx_http_ecma_main_conf_t; 

typedef struct {
    ngx_str_t  handler;
} ngx_http_ecma_loc_conf_t;

static ngx_command_t  ngx_http_ecmascript_commands[] = {
	{ ngx_string("ecmascript"),
    	NGX_HTTP_LOC_CONF|NGX_HTTP_LMT_CONF|NGX_CONF_TAKE1,
    	ngx_http_ecmascript_handler_setup,
    	NGX_HTTP_LOC_CONF_OFFSET,
    	0,
    	NULL },
   ngx_null_command
};


static ngx_http_module_t ngx_http_ecmascript_module_ctx = {
    NULL,   							// preconfiguration
    NULL,   							// postconfiguration
    ngx_http_ecma_create_main_conf,   	// create main configuration
    NULL,   							// init main configuration
    NULL,   							// create server configuration
    NULL,   							// merge server configuration
    ngx_http_ecma_create_loc_conf,   	// create location configuration
    NULL    							// merge location configuration
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

/* The JS error reporter callback. */
void reportError(JSContext *cx, const char *message, JSErrorReport *report){

    fprintf(stderr, "%s:%u \e[31;40m%s\e[0m\n",
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

int file_exist (char *filename){
  return (access(filename, 0 ) != -1);
}

JSBool js_land_eval_file(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval){
    char *filename;
	JSScript *script;
	
    if (!JS_ConvertArguments(cx, argc, argv, "s", &filename)){
		JS_ReportError(cx, "require failed");
        return JS_FALSE;	
	}

	if(!file_exist(filename)){
		JS_ReportError(cx, "File inaccessable: %s", filename);
		return JS_FALSE;	
	}
	
	script = JS_CompileFile(cx, JS_GetGlobalObject(cx), filename);
	if(script == NULL){
        JS_ReportError(cx, "Could not compile %s", filename);
		return JS_FALSE;		
	}
	
	if ( !JS_ExecuteScript(cx, JS_GetGlobalObject(cx), script, rval) ) {
        JS_ReportError(cx, "Could not evaluate script %s", filename);
		return JS_FALSE;
	}

    return JS_TRUE;
}

JSBool js_land_read(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval){
    const char *filename;
	char *contents;
	int size = 0;
	
    if (!JS_ConvertArguments(cx, argc, argv, "s", &filename)){
        return JS_FALSE;	
	}

	FILE *f = fopen(filename, "r");
	if (f == NULL) {
        JS_ReportError(cx, "Could not open file %s", filename);
		return JS_FALSE; 
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	contents = (char *)malloc(size+1);
	if (size != (int)fread(contents, sizeof(char), size, f)){
		free(contents);
        JS_ReportError(cx, "Could not read file %s", filename);
		return JS_FALSE; 		
	}
	fclose(f);
	contents[size] = 0;
	*rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, contents));
    return JS_TRUE;
}

#include <unistd.h>
#include <sys/dir.h>
#include <sys/param.h>

int file_select(struct direct *entry){
	if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0))
		return 0;
	else
		return 1;
}

JSBool js_land_dir(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval){
	char *pathname;
	struct direct **files;
	int count;
	int i;
	jsval f;
	
    if (!JS_ConvertArguments(cx, argc, argv, "s", &pathname)){
        return JS_FALSE;	
	}
	
	JSObject *jsarray = JS_NewArrayObject(cx, 0, NULL);
	if (jsarray == NULL){
        JS_ReportError(cx, "Could not create array while listing %s", pathname);
	    return JS_FALSE;
	}

	// fprintf(stderr,"\nscandir=%s\n",pathname);	
	count = scandir(pathname, &files, file_select, alphasort);
	
	for (i=1; i<count+1; ++i){
		// JS_DefineElement(cx, jsarray, i-1, jsval value, NULL, , uintN flags);
		// fprintf(stderr,"f=%s",files[i-1]->d_name);
		f = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, files[i-1]->d_name));
		JS_SetElement(cx, jsarray, i-1, &f);
	}

	*rval = OBJECT_TO_JSVAL(jsarray);
    return JS_TRUE;
}

JSBool js_land_alert(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval){
    const char *message;
    if (!JS_ConvertArguments(cx, argc, argv, "s", &message)){
        return JS_FALSE;
	}
	
	fprintf(stderr,"\e[31;40m%s\e[0m\n",message);
	
	*rval = JSVAL_VOID;
    return JS_TRUE;
}

JSBool js_land_http(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval) {
	const char 	*method;
	char 		*host;
	char 		*path;
	int			port;
	int  		status;
	char 		*data = NULL;
	char 		body[200000];

    if (!JS_ConvertArguments(cx, argc, argv, "ssis", &method, &host, &port, &path)){
		JS_ReportError(cx, "Invalid args");
        return JS_FALSE;	
	}
	if(argc > 4)
		data = JS_GetStringBytes(JS_ValueToString(cx,argv[4]));

	JSObject *response = JS_NewObject(cx, NULL, NULL, NULL);
	if (response == NULL){
		JS_ReportError(cx, "Could not create response object");
		return JS_FALSE;		
	}

	if (!strcasecmp(method,"put")){
		status = http_query("PUT", host, path, port, "", data, body);
	} else if (!strcasecmp(method,"post")) {
		status = http_query("POST", host, path, port, "", data, body);
	} else if (!strcasecmp(method,"get")) {
		status = http_query("GET", host, path, port, "", NULL, body);
	} else if (!strcasecmp(method,"delete")) {
		status = http_query("DELETE", host, path, port, "", NULL, body);
	} else {
		JS_ReportError(cx, "First arg must be one of GET, PUT, POST, DELETE");
		return JS_FALSE;
	}

	if(!JS_DefineProperty(cx, response, "body", STRING_TO_JSVAL(JS_NewStringCopyZ(cx, body)), NULL, NULL, JSPROP_ENUMERATE)){
		JS_ReportError(cx, "Problem while assigning response body");
		return JS_FALSE;
	}
	if(!JS_DefineProperty(cx, response, "status", INT_TO_JSVAL(status), NULL, NULL, JSPROP_ENUMERATE)){
		JS_ReportError(cx, "Problem while assigning response status");
		return JS_FALSE;
	}
	*rval = OBJECT_TO_JSVAL(response);
	return JS_TRUE;
}

static JSFunctionSpec js_land_global_methods[] = {
    {"rand", 		js_land_rand,    	0,0,0},
    {"system", 		js_land_system,  	1,0,0},
    {"alert", 		js_land_alert, 		1,0,0},
    {"http", 		js_land_http,	 	3,0,0},
    {NULL,NULL,0,0,0}
};

static JSFunctionSpec js_land_file_methods[] = {
    {"dir", 		js_land_dir,    	1,0,0},
    {"read", 		js_land_read,	 	1,0,0},
    {"eval", 		js_land_eval_file, 	1,0,0},
    {NULL,NULL,0,0,0}
};

static ngx_buf_t * js_nginx_str2buf(JSContext *cx, JSString *str, ngx_pool_t *pool, size_t len){
	ngx_buf_t           *b;
	const char          *p;
	
	if (len == 0)
		len = JS_GetStringLength(str);
	
	p = JS_GetStringBytes(str);
	if (p == NULL)
		return NULL;
	
	b = ngx_create_temp_buf(pool, len);
	if (b == NULL)
		return NULL;
	ngx_memcpy(b->last, p, len);
	b->last = b->last + len;
	
	return b;
}

static ngx_int_t ngx_http_ecmascript_handler(ngx_http_request_t *r){
    ngx_int_t     				rc;
    ngx_chain_t  				out;
	ngx_buf_t           		*b;
    JSContext 					*cx;
    JSObject  					*global;
    ngx_http_ecma_main_conf_t  	*ecma_main_config;
    ngx_http_ecma_loc_conf_t  	*ecma_loc_config;
	ngx_str_t                 	path;
	size_t						lenth_to_root_of_path;

	// ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Starting ECMAScript module handler");

    ecma_main_config = ngx_http_get_module_main_conf(r, ngx_http_ecmascript_module);
	ecma_loc_config = ngx_http_get_module_loc_conf(r, ngx_http_ecmascript_module);
   /*
	*
	*   nginx headers
	*
	*/
    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";
    r->headers_out.status = NGX_HTTP_OK;
   /*
	*
	*   the sandbox for this runtime
	*
	*/
    cx = JS_NewContext(ecma_main_config->runtime, 8192);
    if (cx == NULL){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to create JS Context.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
    JS_SetOptions(cx, JSOPTION_VAROBJFIX);
    JS_SetErrorReporter(cx, reportError);
   /*
	*
	*   create an object to use as the global 'this'
	*
	*/
    global = JS_NewObject(cx, NULL, NULL, NULL);
    if (global == NULL){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to create JS global object.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
   /*
	*
	*   add things like Array, Math etc.
	*
	*/
    if (!JS_InitStandardClasses(cx, global)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to populate JS land with standard globals.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}	
   /*
	*
	*   add global properties
	*
	*/
	if (!JS_DefineFunctions(cx, global, js_land_global_methods)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to populate JS land with custom globals");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
   /*
    *
    *   The request object
    *
    */
	JSObject *request = JS_NewObject(cx, NULL, NULL, NULL);
	if (request == NULL){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Could not create request object for some reason");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;		
	}
	// uri prop
	if(!JS_DefineProperty(cx, request, "path", STRING_TO_JSVAL(JS_NewStringCopyN(cx, (char *) r->uri.data, r->uri.len)), NULL, NULL, JSPROP_ENUMERATE)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "path def failed!");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	// querystring
	if(!JS_DefineProperty(cx, request, "queryString", STRING_TO_JSVAL(JS_NewStringCopyN(cx, (char *) r->args.data, r->args.len)), NULL, NULL, JSPROP_ENUMERATE)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "queryString def failed!");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	// request method
	if(!JS_DefineProperty(cx, request, "method", STRING_TO_JSVAL(JS_NewStringCopyN(cx, (char *) r->method_name.data, r->method_name.len)), NULL, NULL, JSPROP_ENUMERATE)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "method def failed!");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	// // accept header
	// if(!JS_DefineProperty(cx, request, "accept", STRING_TO_JSVAL(JS_NewStringCopyN(cx, (char *) r->headers_in.accept->value.data, r->headers_in.accept->value.len)), NULL, NULL, JSPROP_ENUMERATE)){
	// 	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "accept def failed!");
	// 	return NGX_HTTP_INTERNAL_SERVER_ERROR;
	// }
	// remote addr
	if(!JS_DefineProperty(cx, request, "remoteAddress", STRING_TO_JSVAL(JS_NewStringCopyN(cx, (char *) r->connection->addr_text.data, r->connection->addr_text.len)), NULL, NULL, JSPROP_ENUMERATE)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "remoteAddress def failed!");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	// referer
	if (r->headers_in.referer) {
		if(!JS_DefineProperty(cx, request, "referer", STRING_TO_JSVAL(JS_NewStringCopyN(cx, (char *) r->headers_in.referer->value.data, r->headers_in.referer->value.len)), NULL, NULL, JSPROP_ENUMERATE)){
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "referer def failed!");
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}
	}
	// host
	if(!JS_DefineProperty(cx, request, "host", STRING_TO_JSVAL(JS_NewStringCopyN(cx, (char *) r->headers_in.server.data, r->headers_in.server.len)), NULL, NULL, JSPROP_ENUMERATE)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "host def failed!");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
    if (ngx_http_map_uri_to_path(r, &path, &lenth_to_root_of_path, 0) == NULL) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to find document root from reqest");		
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }else{
		if(!JS_DefineProperty(cx, request, "documentRoot", STRING_TO_JSVAL(JS_NewStringCopyN(cx, (char *) path.data, lenth_to_root_of_path)), NULL, NULL, JSPROP_ENUMERATE)){
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "documentRoot def failed!");
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}
	}
   /*
    *
    *   The File object
    *
    */
	JSObject *file = JS_NewObject(cx, NULL, NULL, NULL);
	if (request == NULL){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Could not create File object for some reason");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;		
	}
	if (!JS_DefineFunctions(cx, file, js_land_file_methods)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to populate JS land with custom globals");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	if(!JS_DefineProperty(cx, global, "File", OBJECT_TO_JSVAL(file), NULL, NULL, JSPROP_READONLY)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "path def failed!");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
   /*
	*
	*   load & execute the handler script
	*
	*/
	JSScript *env;
	char *handler_path = (char *)calloc(lenth_to_root_of_path + strlen((char *)ecma_loc_config->handler.data) + 2, sizeof(char));
	strncat(handler_path, (char *)path.data, lenth_to_root_of_path);
	strcat(handler_path, "/");
	strcat(handler_path, (char *)ecma_loc_config->handler.data);
	env = JS_CompileFile(cx, global, handler_path);
	free(handler_path);
	if(env == NULL){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Could not open handler script");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;		
	}	
	jsval result;	
	if ( !JS_ExecuteScript(cx, global, env, &result) ) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Could not evaluate script");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	jsval argv[1];
	argv[0] = OBJECT_TO_JSVAL(request);
	if( !JS_CallFunctionName(cx, global, "process", 1, argv, &result) ){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Could not call 'process' function");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
   /*
	*
	*   ready response
	*
	*/
	// result is a js array of [status, headers, body]
	JSObject *response_array;
	if(!JS_ValueToObject(cx, result, &response_array)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "response was not an object");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	jsval status_val;
	if(!JS_LookupElement(cx,response_array,0,&status_val)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "could not pull status out of response_array");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	jsval headers_val;
	if(!JS_LookupElement(cx,response_array,1,&headers_val)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "could not pull headers out of response_array");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	jsval body_val;
	if(!JS_LookupElement(cx,response_array,2,&body_val)){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "could not pull body out of response_array");
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "5");
	JSString *str = JS_ValueToString(cx,body_val);
	b = js_nginx_str2buf(cx, str, r->pool, JS_GetStringLength(str));
ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "6");
	out.buf = b;
	out.next = NULL;
	r->headers_out.content_length_n = b->last - b->pos;
	r->headers_out.status = JSVAL_TO_INT(status_val);
    b->last_buf = 1;
   /*
	*
	*   clean up context
	*
	*/
	JS_DestroyScript(cx, env);
    JS_DestroyContext(cx);
   /*
	*
	*   write response
	*
	*/
	rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "No body");
        return rc;
    }
    return ngx_http_output_filter(r, &out);
}

static void * ngx_http_ecma_create_main_conf(ngx_conf_t *cf){
    ngx_http_ecma_main_conf_t  *ecma_main_config;
    ecma_main_config = ngx_pcalloc(cf->pool, sizeof(ngx_http_ecma_main_conf_t));
    if (ecma_main_config == NULL) {
        return NGX_CONF_ERROR;
    }
    return ecma_main_config;
}

static void * ngx_http_ecma_create_loc_conf(ngx_conf_t *cf){
	ngx_http_ecma_loc_conf_t *ecma_loc_config;
	ecma_loc_config = ngx_pcalloc(cf->pool, sizeof(ngx_http_ecma_loc_conf_t));
	if (ecma_loc_config == NULL)
		return NGX_CONF_ERROR;
	return ecma_loc_config;
}


static char * ngx_http_ecmascript_handler_setup(ngx_conf_t *cf, ngx_command_t *cmd, void *conf){
	static JSRuntime 			*rt;
    ngx_http_core_loc_conf_t  	*core_loc_config;
	ngx_http_ecma_main_conf_t 	*ecma_main_config;
	ngx_http_ecma_loc_conf_t 	*ecma_loc_config = conf;
	ngx_str_t 					*value;
   /*
	*
	*   assign handler
	*
	*/
	core_loc_config = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    core_loc_config->handler = ngx_http_ecmascript_handler;
   /*
	*
	*   setup handler script for location
	*
	*/
	value = cf->args->elts;
	if (ecma_loc_config->handler.data){
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "You can only have one handler per location \"%V\"", &value[1]);
		return NGX_CONF_ERROR;
	}
	ecma_loc_config->handler = value[1];
   /*
	*
	*   setup a runtime to use
	*
	*/
	ecma_main_config = ngx_http_conf_get_module_main_conf(cf, ngx_http_ecmascript_module);
	if (ecma_main_config->runtime == NULL){
		rt = JS_NewRuntime(8L * 1024L * 1024L);
		if (rt == NULL){
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "Failed to create spidermonkey runtime");
			return NGX_CONF_ERROR;
		}
		ecma_main_config->runtime = rt;
	}
    return NGX_CONF_OK;
}

