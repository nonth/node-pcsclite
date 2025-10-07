#include "pcsclite.h"
#include "cardreader.h"

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
    PCSCLite::Init(env, exports);
    CardReader::Init(env, exports);
    return exports;
}

NODE_API_MODULE(pcsclite, InitAll)
