/*
 * Preprocessor HTML templating language for C.
 */
#pragma once
#include <stdbool.h>
#include <stdlib.h>

/*
#define VSPRINTF(pattern) \
( \
    char b##__COUNTER__[128], \
    va_list a##__COUNTER__, \
    va_start(a##__COUNTER__, pattern), \
    vsnprintf(b##__COUNTER__, sizeof(b##__COUNTER__), (pattern), a##__COUNTER__), \
    va_end(a##__COUNTER__), \
    b##__COUNTER__; \
)
 */

#define a html_tag("a", true);
#define abbr html_tag("abbr", true);
#define address html_tag("address", true);
#define area html_tag("area", false);
#define article html_tag("article", true);
#define aside html_tag("aside", true);
#define _(pattern, ...) html_attr(pattern, ##__VA_ARGS__);
#define audio html_tag("audio", true);
#define b html_tag("b", true);
#define base html_tag("base", false);
#define bdi html_tag("bdi", true);
#define bdo html_tag("bdo", true);
#define blockquote html_tag("blockquote", true);
#define body html_tag("body", true);
#define br html_tag("br", false);
#define button html_tag("button", true);
#define canvas html_tag("canvas", true);
#define caption html_tag("caption", true);
#define cite html_tag("cite", true);
#define code html_tag("code", true);
#define col html_tag("col", false);
#define colgroup html_tag("colgroup", true);
#define data html_tag("data", true);
#define datalist html_tag("datalist", true);
#define doctype html_doctype();
#define dd html_tag("dd", true);
#define del html_tag("del", true);
#define details html_tag("details", true);
#define dfn html_tag("dfn", true);
#define dialog html_tag("dialog", true);
#define div html_tag("div", true);
#define dl html_tag("dl", true);
#define dt html_tag("dt", true);
#define em html_tag("em", true);
#define embed html_tag("embed", false);
#define end html_end();
#define fieldset html_tag("fieldset", true);
#define figcaption html_tag("figcaption", true);
#define figure html_tag("figure", true);
#define footer html_tag("footer", true);
#define form html_tag("form", true);
#define h1 html_tag("h1", true);
#define head html_tag("head", true);
#define header html_tag("header", true);
#define hgroup html_tag("hgroup", true);
#define hr html_tag("hr", false);
#define html html_tag("html", true);
#define i html_tag("i", true);
#define iframe html_tag("iframe", true);
#define img html_tag("img", false);
#define input html_tag("input", false);
#define ins html_tag("ins", true);
#define kbd html_tag("kbd", true);
#define keygen html_tag("keygen", false);
#define label html_tag("label", true);
#define legend html_tag("legend", true);
#define li html_tag("li", true);
#define link html_tag("link", false);
#define main html_tag("main", true);
#define map html_tag("map", true);
#define mark html_tag("mark", true);
#define menu html_tag("menu", true);
#define menuitem html_tag("menuitem", true);
#define meta html_tag("meta", false);
#define meter html_tag("meter", true);
#define nav html_tag("nav", true);
#define noscript html_tag("noscript", true);
#define object html_tag("object", true);
#define ol html_tag("ol", true);
#define optgroup html_tag("optgroup", true);
#define option html_tag("option", true);
#define output html_tag("output", true);
#define p html_tag("p", true);
#define param html_tag("param", false);
#define picture html_tag("picture", true);
#define pre html_tag("pre", true);
#define progress html_tag("progress", true);
#define q html_tag("q", true);
#define rp html_tag("rp", true);
#define rt html_tag("rt", true);
#define ruby html_tag("ruby", true);
#define s html_tag("s", true);
#define samp html_tag("samp", true);
#define script html_tag("script", true);
#define section html_tag("section", true);
#define select html_tag("select", true);
#define small html_tag("small", true);
#define source html_tag("source", false);
#define span html_tag("span", true);
#define strong html_tag("strong", true);
#define style html_tag("style", true);
#define sub html_tag("sub", true);
#define summary html_tag("summary", true);
#define sup html_tag("sup", true);
#define svg html_tag("svg", true);
#define table html_tag("table", true);
#define tbody html_tag("tbody", true);
#define td html_tag("td", true);
#define template html_tag("template", true); // "template" is a C++ keyword.
#define text(pattern, ...) html_text(pattern, ##__VA_ARGS__);
#define textarea html_tag("textarea", true);
#define tfoot html_tag("tfoot", true);
#define th html_tag("th", true);
#define thead html_tag("thead", true);
#define time html_tag("time", true);
#define title html_tag("title", true);
#define tr html_tag("tr", true);
#define track html_tag("track", false);
#define u html_tag("u", true);
#define ul html_tag("ul", true);
#define var html_tag("var", true);
#define video html_tag("video", true);
#define wbr html_tag("wbr", false);

#define boilerplate(t, ...)	html_boilerplate(t, ##__VA_ARGS__);
#define end_boilerplate	html_end_boilerplate();

extern void html_attr(const char * pattern, ...);
extern void html_boilerplate(const char * t, ...);
extern void html_doctype();
extern void html_end();
extern void html_end_boilerplate();
extern void html_tag(const char *, bool);
extern void html_text(const char * pattern, ...);

extern void get_button(const char * t, const char * pattern, ...);
extern void post_button(const char * t, const char * pattern, ...);
extern void gm_web_set_request(void * context);
extern void gm_web_send_to_client (const char *d, size_t size);
extern void gm_web_finish(const char *d, size_t size);
