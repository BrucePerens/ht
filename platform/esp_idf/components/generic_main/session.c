#include "generic_main.h"

static void
free_context(void * context)
{
  gm_session_context_t * const s = context;

  cJSON_Delete(s->json);
  free(s->user_name);
  free(s);
}

void
gm_session(httpd_req_t * req)
{
  if ( req->sess_ctx ) {
    // We already have session context for the user. Don't bother decoding our
    // cookie during this request, that's a relatively heavy-weight task with
    // conversion from Base-64, and decryption, and parsing JSON, and we have
    // all of that data in-hand.
  }
  else {
    gm_session_context_t * const s = malloc(sizeof(gm_session_context_t));
    req->sess_ctx = s;
    req->free_ctx = free_context;
    cJSON * json = gm_read_cookie(req);
    if ( json ) {
      s->json = json;
      const cJSON * const name = cJSON_GetObjectItemCaseSensitive(json, "name");
      if ( name ) {
        if ( cJSON_IsString(name) && name->valuestring != NULL ) {
          if ( gm_get_user_data(name->valuestring, &s->user_data) ) {
            s->user_name = strdup(name->valuestring);
          }
        }
      }
    }
  }
}
