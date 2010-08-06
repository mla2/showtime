/*
 *  JSAPI <-> Navigator page object
 *  Copyright (C) 2010 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <regex.h>
#include "js.h"


#include "backend/backend.h"
#include "backend/backend_prop.h"
#include "navigator.h"
#include "misc/string.h"

static struct js_route_list js_routes;
static struct js_searcher_list js_searchers;

/**
 *
 */
typedef struct js_route {
  LIST_ENTRY(js_route) jsr_global_link;
  LIST_ENTRY(js_route) jsr_plugin_link;
  char *jsr_pattern;
  regex_t jsr_regex;
  jsval jsr_openfunc;
  int jsr_prio;
} js_route_t;


/**
 *
 */
typedef struct js_searcher {
  LIST_ENTRY(js_searcher) jss_global_link;
  LIST_ENTRY(js_searcher) jss_plugin_link;
  jsval jss_openfunc;
} js_searcher_t;


/**
 *
 */
typedef struct js_model {
  int jm_refcount;

  char **jm_args;

  jsval jm_openfunc;

  prop_t *jm_nodes;

  prop_t *jm_loading;
  prop_t *jm_type;
  prop_t *jm_title;
  prop_t *jm_entries;
  prop_t *jm_url;

  prop_courier_t *jm_pc;
  prop_sub_t *jm_nodesub;

  int jm_run;

  jsval jm_paginator;
  
  JSContext *jm_cx;

} js_model_t;


static JSObject *make_model_object(JSContext *cx, js_model_t *jm);

/**
 *
 */
static js_model_t *
js_model_create(jsval openfunc)
{
  js_model_t *jm = calloc(1, sizeof(js_model_t));
  jm->jm_refcount = 1;
  jm->jm_run = 1;
  jm->jm_openfunc = openfunc;
  return jm;
}


/**
 *
 */
static void
js_model_destroy(js_model_t *jm)
{
  if(jm->jm_args)
    strvec_free(jm->jm_args);

  if(jm->jm_nodesub != NULL)
    prop_unsubscribe(jm->jm_nodesub);

  if(jm->jm_loading)   prop_ref_dec(jm->jm_loading);
  if(jm->jm_nodes)     prop_ref_dec(jm->jm_nodes);
  if(jm->jm_type)      prop_ref_dec(jm->jm_type);
  if(jm->jm_title)     prop_ref_dec(jm->jm_title);
  if(jm->jm_entries)   prop_ref_dec(jm->jm_entries);
  if(jm->jm_url)       prop_ref_dec(jm->jm_url);

  if(jm->jm_pc != NULL)
    prop_courier_destroy(jm->jm_pc);

  free(jm);
}


/**
 *
 */
static void
js_model_release(js_model_t *jm)
{
  if(atomic_add(&jm->jm_refcount, -1) == 1)
    js_model_destroy(jm);
}


/**
 *
 */
static JSBool 
js_setTitle(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  js_prop_set_from_jsval(cx, jm->jm_title, *vp);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setEntries(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  js_prop_set_from_jsval(cx, jm->jm_entries, *vp);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setType(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  js_prop_set_from_jsval(cx, jm->jm_type, *vp);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setURL(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  js_prop_set_from_jsval(cx, jm->jm_url, *vp);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setLoading(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{ 
  js_model_t *jm = JS_GetPrivate(cx, obj);
  JSBool on;

  if(!JS_ValueToBoolean(cx, *vp, &on))
    return JS_FALSE;

  prop_set_int(jm->jm_loading, on);
  return JS_TRUE;
}



/**
 *
 */
static JSBool 
js_appendItem(JSContext *cx, JSObject *obj, uintN argc,
	      jsval *argv, jsval *rval)
{
  const char *url;
  const char *type;
  JSObject *metaobj = NULL;
  prop_t *item;
  js_model_t *parent = JS_GetPrivate(cx, obj);

  if(!JS_ConvertArguments(cx, argc, argv, "ss/o", &url, &type, &metaobj))
    return JS_FALSE;

  item = prop_create(NULL, NULL);

  if(metaobj)
    js_prop_from_object(cx, metaobj, prop_create(item, "metadata"));

  prop_set_string(prop_create(item, "url"), url);
  prop_set_string(prop_create(item, "type"), type);

  if(prop_set_parent(item, parent->jm_nodes))
    prop_destroy(item);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_appendModel(JSContext *cx, JSObject *obj, uintN argc,
	       jsval *argv, jsval *rval)
{
  js_model_t *parent = JS_GetPrivate(cx, obj);
  const char *type;
  JSObject *metaobj = NULL;
  char url[URL_MAX];
  prop_t *item, *metadata;
  js_model_t *jm;
  JSObject *robj;

  if(!JS_ConvertArguments(cx, argc, argv, "s/o", &type, &metaobj))
    return JS_FALSE;

  item = prop_create(NULL, NULL);

  backend_prop_make(item, url, sizeof(url));
 
  metadata = prop_create(item, "metadata");

  if(metaobj)
    js_prop_from_object(cx, metaobj, metadata);

  prop_set_string(prop_create(item, "url"), url);

  jm = js_model_create(JSVAL_VOID);

  prop_ref_inc(jm->jm_nodes   = prop_create(item,     "nodes"));
  prop_ref_inc(jm->jm_type    = prop_create(item,     "type"));
  prop_ref_inc(jm->jm_entries = prop_create(metadata, "entries"));

  prop_set_string(jm->jm_type, type);

  if(prop_set_parent(item, parent->jm_nodes))
    prop_destroy(item);

  robj = make_model_object(cx, jm);

  *rval = OBJECT_TO_JSVAL(robj);
  return JS_TRUE;
}





/**
 *
 */
static JSBool 
js_setPaginator(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);

  if(!JSVAL_IS_OBJECT(*vp) || 
     !JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(*vp))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  jm->jm_paginator = *vp;
  JS_AddNamedRoot(cx, &jm->jm_paginator, "paginator");
  return JS_TRUE;
}


/**
 *
 */
static JSFunctionSpec page_functions[] = {
    JS_FS("appendItem",         js_appendItem,   3, 0, 0),
    JS_FS("appendModel",        js_appendModel,  2, 0, 0),
    JS_FS_END
};


/**
 *
 */
static void
model_finalize(JSContext *cx, JSObject *obj)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  js_model_release(jm);
}


static JSClass model_class = {
  "model", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, model_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static JSObject *
make_model_object(JSContext *cx, js_model_t *jm)
{
  JSObject *obj = JS_NewObjectWithGivenProto(cx, &model_class, NULL, NULL);

  JS_SetPrivate(cx, obj, jm);
  atomic_add(&jm->jm_refcount, 1);

  JS_DefineFunctions(cx, obj, page_functions);

  if(jm->jm_title != NULL)
    JS_DefineProperty(cx, obj, "title", JSVAL_VOID,
		      NULL, js_setTitle, JSPROP_PERMANENT);

  if(jm->jm_entries != NULL)
    JS_DefineProperty(cx, obj, "entries", JSVAL_VOID,
		      NULL, js_setEntries, JSPROP_PERMANENT);

  if(jm->jm_type != NULL)
    JS_DefineProperty(cx, obj, "type", JSVAL_VOID,
		      NULL, js_setType, JSPROP_PERMANENT);
   
  if(jm->jm_loading != NULL)
    JS_DefineProperty(cx, obj, "loading", BOOLEAN_TO_JSVAL(1),
		      NULL, js_setLoading, JSPROP_PERMANENT);

  if(jm->jm_url != NULL)
    JS_DefineProperty(cx, obj, "url", JSVAL_VOID,
		      NULL, js_setURL, JSPROP_PERMANENT);

  JS_DefineProperty(cx, obj, "paginator", JSVAL_VOID,
		    NULL, js_setPaginator, JSPROP_PERMANENT);
  return obj;
}


/**
 *
 */
static void
js_open_invoke(JSContext *cx, js_model_t *jm)
{
  jsval *argv, result;
  void *mark;
  char argfmt[10];
  int i = 0, argc;
  JSObject *obj = make_model_object(cx, jm);

  if(jm->jm_args != NULL) {
    argfmt[0] = 'o';
    while(i < 8 && jm->jm_args[i]) {
      argfmt[i+1] = 's';
      i++;
    }
    argfmt[i+1] = 0;
    argc = i+1;
    argv = JS_PushArguments(cx, &mark, argfmt, obj,
			    i > 0 ? jm->jm_args[0] : "",
			    i > 1 ? jm->jm_args[1] : "",
			    i > 2 ? jm->jm_args[2] : "",
			    i > 3 ? jm->jm_args[3] : "",
			    i > 4 ? jm->jm_args[4] : "",
			    i > 5 ? jm->jm_args[5] : "",
			    i > 6 ? jm->jm_args[6] : "",
			    i > 7 ? jm->jm_args[7] : "");

  } else {
    argv = JS_PushArguments(cx, &mark, "o", obj);
    argc = 2;
  }
  if(argv == NULL)
    return;
  JS_CallFunctionValue(cx, NULL, jm->jm_openfunc, argc, argv, &result);
  JS_PopArguments(cx, mark);
}


/**
 *
 */
static void
js_model_fill(JSContext *cx, js_model_t *jm)
{
  jsval result;

  if(!jm->jm_paginator)
    return;

  JS_BeginRequest(cx);
  JS_CallFunctionValue(cx, NULL, jm->jm_paginator, 0, NULL, &result);
  JS_EndRequest(cx);
}


/**
 *
 */
static void *
js_open_trampoline(void *arg)
{
  js_model_t *jm = arg;
  
  JSContext *cx = js_newctx();
  JS_BeginRequest(cx);

  js_open_invoke(cx, jm);

  if(jm->jm_paginator) {
    jm->jm_cx = cx;

    while(jm->jm_run) {
      jsrefcount s = JS_SuspendRequest(cx);
      prop_courier_wait(jm->jm_pc);
      JS_ResumeRequest(cx, s);
    }
    JS_RemoveRoot(cx, &jm->jm_paginator);
  }

  js_model_release(jm);

  JS_DestroyContext(cx);
  return NULL;
}


/**
 *
 */
static void
js_model_nodesub(void *opaque, prop_event_t event, ...)
{
  js_model_t *jm = opaque;
  va_list ap;
  event_t *e;

  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    jm->jm_run = 0;
    break;

  case PROP_EXT_EVENT:
    e = va_arg(ap, event_t *);
    if(e->e_type_x == EVENT_APPEND_REQUEST)
      js_model_fill(jm->jm_cx, jm);

    break;
  }
  va_end(ap);
}


/**
 *
 */
struct nav_page *
js_backend_open(struct backend *be, struct navigator *nav, 
		const char *url, const char *view,
		char *errbuf, size_t errlen)
{
  js_route_t *jsr;
  regmatch_t matches[8];
  int i;
  nav_page_t *np;
  js_model_t *jm;
  prop_t *model, *meta;

  LIST_FOREACH(jsr, &js_routes, jsr_global_link)
    if(!regexec(&jsr->jsr_regex, url, 8, matches, 0))
      break;

  if(jsr == NULL)
    return BACKEND_NOURI;

  np = nav_page_create(nav, url, view, NAV_PAGE_DONT_CLOSE_ON_BACK);

  jm = js_model_create(jsr->jsr_openfunc);

  for(i = 1; i < 8; i++)
    if(matches[i].rm_so != -1)
      strvec_addpn(&jm->jm_args, url + matches[i].rm_so, 
		   matches[i].rm_eo - matches[i].rm_so);
  
  model = prop_create(np->np_prop_root, "model");
  meta  = prop_create(model, "metadata");

  prop_ref_inc(jm->jm_loading = prop_create(model, "loading"));
  prop_ref_inc(jm->jm_nodes   = prop_create(model, "nodes"));
  prop_ref_inc(jm->jm_type    = prop_create(model, "type"));
  prop_ref_inc(jm->jm_title   = prop_create(meta,  "title"));
  prop_ref_inc(jm->jm_entries = prop_create(meta,  "entries"));
  prop_ref_inc(jm->jm_url     = prop_create(np->np_prop_root, "url"));


  jm->jm_pc = prop_courier_create_waitable();

  jm->jm_nodesub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, js_model_nodesub, jm,
		   PROP_TAG_ROOT, jm->jm_nodes,
		   PROP_TAG_COURIER, jm->jm_pc,
		   NULL);

  hts_thread_create_detached("jsmodel", js_open_trampoline, jm);
  prop_set_int(jm->jm_loading, 1);
  return np;
}


/**
 *
 */
void
js_backend_search(struct backend *be, struct prop *model, const char *query)
{
  js_searcher_t *jss;
  prop_t *nodes = prop_create(model, "nodes");
  js_model_t *jm;

  LIST_FOREACH(jss, &js_searchers, jss_global_link) {
    jm = js_model_create(jss->jss_openfunc);

    strvec_addp(&jm->jm_args, query);
    prop_ref_inc(jm->jm_nodes = nodes);

    hts_thread_create_detached("jsmodel", js_open_trampoline, jm);
  }
}


/**
 *
 */
static int
jsr_cmp(const js_route_t *a, const js_route_t *b)
{
  return b->jsr_prio - a->jsr_prio;
}


/**
 *
 */
JSBool 
js_addURI(JSContext *cx, JSObject *obj, uintN argc, 
	  jsval *argv, jsval *rval)
{
  const char *str;
  js_route_t *jsr;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  str = JS_GetStringBytes(JS_ValueToString(cx, argv[0]));

  if(!JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(argv[1]))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  if(str[0] != '^') {
    int l = strlen(str);
    char *s = alloca(l + 2);
    s[0] = '^';
    memcpy(s+1, str, l+1);
    str = s;
  }

  LIST_FOREACH(jsr, &js_routes, jsr_global_link)
    if(!strcmp(str, jsr->jsr_pattern)) {
      JS_ReportError(cx, "URL already routed");
      return JS_FALSE;
    }
  
  jsr = calloc(1, sizeof(js_route_t));

  if(regcomp(&jsr->jsr_regex, str, REG_EXTENDED | REG_ICASE)) {
    free(jsr);
    JS_ReportError(cx, "Invalid regular expression");
    return JS_FALSE;
  }
  
  jsr->jsr_pattern = strdup(str);
  jsr->jsr_prio = strcspn(str, "()[].*?+$") ?: INT32_MAX;
  
  LIST_INSERT_SORTED(&js_routes, jsr, jsr_global_link, jsr_cmp);
  LIST_INSERT_HEAD(&jsp->jsp_routes, jsr, jsr_plugin_link);

  TRACE(TRACE_DEBUG, "JS", "Add route for %s", str);

  jsr->jsr_openfunc = argv[1];
  JS_AddNamedRoot(cx, &jsr->jsr_openfunc, "routeduri");

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
JSBool 
js_addSearcher(JSContext *cx, JSObject *obj, uintN argc, 
	       jsval *argv, jsval *rval)
{
  js_searcher_t *jss;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  if(!JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(argv[0]))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }
  
  jss = calloc(1, sizeof(js_searcher_t));

  LIST_INSERT_HEAD(&js_searchers, jss, jss_global_link);
  LIST_INSERT_HEAD(&jsp->jsp_searchers, jss, jss_plugin_link);

  jss->jss_openfunc = argv[0];
  JS_AddNamedRoot(cx, &jss->jss_openfunc, "searcher");

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static void
js_route_delete(JSContext *cx, js_route_t *jsr)
{
  JS_RemoveRoot(cx, &jsr->jsr_openfunc);

  LIST_REMOVE(jsr, jsr_global_link);
  LIST_REMOVE(jsr, jsr_plugin_link);

  regfree(&jsr->jsr_regex);

  free(jsr->jsr_pattern);
  free(jsr);
}


/**
 *
 */
static void
js_searcher_delete(JSContext *cx, js_searcher_t *jss)
{
  JS_RemoveRoot(cx, &jss->jss_openfunc);

  LIST_REMOVE(jss, jss_global_link);
  LIST_REMOVE(jss, jss_plugin_link);
  free(jss);
}


/**
 *
 */
void
js_page_flush_from_plugin(JSContext *cx, js_plugin_t *jsp)
{
  js_route_t *jsr;
  js_searcher_t *jss;

  while((jsr = LIST_FIRST(&jsp->jsp_routes)) != NULL)
    js_route_delete(cx, jsr);

  while((jss = LIST_FIRST(&jsp->jsp_searchers)) != NULL)
    js_searcher_delete(cx, jss);
}