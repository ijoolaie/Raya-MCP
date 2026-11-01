#include <node_api.h>

static napi_value get_napi_version(napi_env env, napi_callback_info info) {
  (void)info;
  uint32_t version = 0;
  if (napi_get_version(env, &version) != napi_ok) return NULL;
  napi_value result;
  if (napi_create_uint32(env, version, &result) != napi_ok) return NULL;
  return result;
}

static napi_value init(napi_env env, napi_value exports) {
  napi_value fn;
  if (napi_create_function(env, "napiVersion", NAPI_AUTO_LENGTH, get_napi_version, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "napiVersion", fn) != napi_ok) return NULL;
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
