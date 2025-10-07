#include "pcsclite.h"
#include "common.h"
#include <cassert>

Napi::Object PCSCLite::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "PCSCLite", {
        InstanceMethod("start", &PCSCLite::Start),
        InstanceMethod("close", &PCSCLite::Close)
    });

    Napi::FunctionReference* constructor = new Napi::FunctionReference();
    *constructor = Napi::Persistent(func);
    env.SetInstanceData(constructor);

    exports.Set("PCSCLite", func);
    return exports;
}

PCSCLite::PCSCLite(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<PCSCLite>(info),
      m_card_context(0),
      m_card_reader_state(),
      m_status_thread(0),
      m_state(0) {

    assert(uv_mutex_init(&m_mutex) == 0);
    assert(uv_cond_init(&m_cond) == 0);

    // TODO: consider removing this Windows workaround that should not be needed anymore
#ifdef _WIN32
    HKEY hKey;
    DWORD startStatus, datacb = sizeof(DWORD);
    LONG _res;
    _res = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Services\\SCardSvr", 0, KEY_READ, &hKey);
    if (_res != ERROR_SUCCESS) {
        printf("Reg Open Key exited with %d\n", _res);
        goto postServiceCheck;
    }
    _res = RegQueryValueEx(hKey, "Start", NULL, NULL, (LPBYTE)&startStatus, &datacb);
    if (_res != ERROR_SUCCESS) {
        printf("Reg Query Value exited with %d\n", _res);
        goto postServiceCheck;
    }
    if (startStatus != 2) {
        SHELLEXECUTEINFO seInfo = {0};
        seInfo.cbSize = sizeof(SHELLEXECUTEINFO);
        seInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
        seInfo.hwnd = NULL;
        seInfo.lpVerb = "runas";
        seInfo.lpFile = "sc.exe";
        seInfo.lpParameters = "config SCardSvr start=auto";
        seInfo.lpDirectory = NULL;
        seInfo.nShow = SW_SHOWNORMAL;
        seInfo.hInstApp = NULL;
        if (!ShellExecuteEx(&seInfo)) {
            printf("Shell Execute failed with %d\n", GetLastError());
            goto postServiceCheck;
        }
        WaitForSingleObject(seInfo.hProcess, INFINITE);
        CloseHandle(seInfo.hProcess);
    }
postServiceCheck:
#endif // _WIN32

    LONG result;
    // TODO: consider removing this do-while Windows workaround that should not be needed anymore
    do {
        // TODO: make dwScope (now hard-coded to SCARD_SCOPE_SYSTEM) customisable
        result = SCardEstablishContext(SCARD_SCOPE_SYSTEM,
                                            NULL,
                                            NULL,
                                            &m_card_context);
    } while(result == SCARD_E_NO_SERVICE || result == SCARD_E_SERVICE_STOPPED);

    if (result != SCARD_S_SUCCESS) {
        Napi::Error::New(info.Env(), error_msg("SCardEstablishContext", result)).ThrowAsJavaScriptException();
    } else {
        m_card_reader_state.szReader = "\\\\?PnP?\\Notification";
        m_card_reader_state.dwCurrentState = SCARD_STATE_UNAWARE;
        result = SCardGetStatusChange(m_card_context,
                                      0,
                                      &m_card_reader_state,
                                      1);

        if ((result != SCARD_S_SUCCESS) && (result != (LONG)SCARD_E_TIMEOUT)) {
            Napi::Error::New(info.Env(), error_msg("SCardGetStatusChange", result)).ThrowAsJavaScriptException();
        } else {
            m_pnp = !(m_card_reader_state.dwEventState & SCARD_STATE_UNKNOWN);
        }
    }
}

PCSCLite::~PCSCLite() {
    if (m_status_thread) {
        SCardCancel(m_card_context);
        assert(uv_thread_join(&m_status_thread) == 0);
    }

    if (m_card_context) {
        SCardReleaseContext(m_card_context);
    }

    uv_cond_destroy(&m_cond);
    uv_mutex_destroy(&m_mutex);
}

Napi::Value PCSCLite::Start(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "Callback function required").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Function cb = info[0].As<Napi::Function>();

    AsyncBaton *async_baton = new AsyncBaton();
    async_baton->async.data = async_baton;
    async_baton->callback.Reset();
    async_baton->callback = Napi::Persistent(cb);
    async_baton->pcsclite = this;
    async_baton->env = env;

    uv_async_init(uv_default_loop(), &async_baton->async, (uv_async_cb)HandleReaderStatusChange);
    int ret = uv_thread_create(&m_status_thread, HandlerFunction, async_baton);
    assert(ret == 0);

    return env.Undefined();
}

Napi::Value PCSCLite::Close(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    LONG result = SCARD_S_SUCCESS;
    if (m_pnp) {
        if (m_status_thread) {
            uv_mutex_lock(&m_mutex);
            if (m_state == 0) {
                int ret;
                int times = 0;
                m_state = 1;
                do {
                    result = SCardCancel(m_card_context);
                    ret = uv_cond_timedwait(&m_cond, &m_mutex, 10000000);
                } while ((ret != 0) && (++ times < 5));
            }

            uv_mutex_unlock(&m_mutex);
        }
    } else {
        m_state = 1;
    }

    if (m_status_thread) {
        assert(uv_thread_join(&m_status_thread) == 0);
        m_status_thread = 0;
    }

    return Napi::Number::New(env, result);
}

void PCSCLite::HandleReaderStatusChange(uv_async_t *handle) {
    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    AsyncResult* ar = async_baton->async_result;
    Napi::Env env(async_baton->env);
    Napi::HandleScope scope(env);

    if (async_baton->pcsclite->m_state == 1) {
        // Swallow events : Listening thread was cancelled by user.
    } else if ((ar->result == SCARD_S_SUCCESS) ||
               (ar->result == (LONG)SCARD_E_NO_READERS_AVAILABLE)) {
        std::vector<napi_value> argv = {
            env.Undefined(),
            Napi::Buffer<char>::Copy(env, ar->readers_name, ar->readers_name_length)
        };

        async_baton->callback.Call(argv);
    } else {
        std::vector<napi_value> argv = { Napi::Error::New(env, ar->err_msg).Value() };
        async_baton->callback.Call(argv);
    }

    // Do exit, after throwing last events
    if (ar->do_exit) {
        // necessary otherwise UV will block
        uv_close(reinterpret_cast<uv_handle_t*>(&async_baton->async), CloseCallback);
        return;
    }

    /* reset AsyncResult */
#ifdef SCARD_AUTOALLOCATE
    PCSCLite* pcsclite = async_baton->pcsclite;
    SCardFreeMemory(pcsclite->m_card_context, ar->readers_name);
#else
    delete [] ar->readers_name;
#endif
    ar->readers_name = NULL;
    ar->readers_name_length = 0;
    ar->result = SCARD_S_SUCCESS;
}

void PCSCLite::HandlerFunction(void* arg) {
    LONG result = SCARD_S_SUCCESS;
    AsyncBaton* async_baton = static_cast<AsyncBaton*>(arg);
    PCSCLite* pcsclite = async_baton->pcsclite;
    async_baton->async_result = new AsyncResult();

    while (!pcsclite->m_state) {
        /* Get card readers */
        result = pcsclite->get_card_readers(pcsclite, async_baton->async_result);
        if (result == (LONG)SCARD_E_NO_READERS_AVAILABLE) {
            result = SCARD_S_SUCCESS;
        }

        /* Store the result in the baton */
        async_baton->async_result->result = result;
        if (result != SCARD_S_SUCCESS) {
            async_baton->async_result->err_msg = error_msg("SCardListReaders",
                                                           result);
        }

        /* Notify the nodejs thread */
        uv_async_send(&async_baton->async);

        if (result == SCARD_S_SUCCESS) {
            if (pcsclite->m_pnp) {
                /* Set current status */
                pcsclite->m_card_reader_state.dwCurrentState =
                    pcsclite->m_card_reader_state.dwEventState;
                /* Start checking for status change */
                result = SCardGetStatusChange(pcsclite->m_card_context,
                                              INFINITE,
                                              &pcsclite->m_card_reader_state,
                                              1);

                uv_mutex_lock(&pcsclite->m_mutex);
                async_baton->async_result->result = result;
                if (pcsclite->m_state) {
                    uv_cond_signal(&pcsclite->m_cond);
                }

                if (result != SCARD_S_SUCCESS) {
                    pcsclite->m_state = 2;
                    async_baton->async_result->err_msg =
                      error_msg("SCardGetStatusChange", result);
                }

                uv_mutex_unlock(&pcsclite->m_mutex);
            } else {
                /*  If PnP is not supported, just wait for 1 second */
                Sleep(1000);
            }
        } else {
            /* Error on last card access, stop monitoring */
            pcsclite->m_state = 2;
        }
    }

    async_baton->async_result->do_exit = true;
    uv_async_send(&async_baton->async);
}

void PCSCLite::CloseCallback(uv_handle_t *handle) {
    /* cleanup process */
    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    AsyncResult* ar = async_baton->async_result;
#ifdef SCARD_AUTOALLOCATE
    PCSCLite* pcsclite = async_baton->pcsclite;
    SCardFreeMemory(pcsclite->m_card_context, ar->readers_name);
#else
    delete [] ar->readers_name;
#endif
    delete ar;
    async_baton->callback.Reset();
    delete async_baton;
}

LONG PCSCLite::get_card_readers(PCSCLite* pcsclite, AsyncResult* async_result) {
    DWORD readers_name_length;
    LPTSTR readers_name;

    LONG result = SCARD_S_SUCCESS;

    /* Reset the readers_name in the baton */
    async_result->readers_name = NULL;
    async_result->readers_name_length = 0;

#ifdef SCARD_AUTOALLOCATE
    readers_name_length = SCARD_AUTOALLOCATE;
    result = SCardListReaders(pcsclite->m_card_context,
                              NULL,
                              (LPTSTR)&readers_name,
                              &readers_name_length);
#else
    /* Find out ReaderNameLength */
    result = SCardListReaders(pcsclite->m_card_context,
                              NULL,
                              NULL,
                              &readers_name_length);
    if (result != SCARD_S_SUCCESS) {
        return result;
    }

    /*
     * Allocate Memory for ReaderName and retrieve all readers in the terminal
     */
    readers_name = new char[readers_name_length];
    result = SCardListReaders(pcsclite->m_card_context,
                              NULL,
                              readers_name,
                              &readers_name_length);
#endif

    if (result != SCARD_S_SUCCESS) {
#ifndef SCARD_AUTOALLOCATE
        delete [] readers_name;
#endif
        readers_name = NULL;
        readers_name_length = 0;
#ifndef SCARD_AUTOALLOCATE
        /* Retry in case of insufficient buffer error */
        if (result == (LONG)SCARD_E_INSUFFICIENT_BUFFER) {
            result = get_card_readers(pcsclite, async_result);
        }
#endif
        if (result == SCARD_E_NO_SERVICE || result == SCARD_E_SERVICE_STOPPED) {
            SCardReleaseContext(pcsclite->m_card_context);
            SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &pcsclite->m_card_context);
            result = get_card_readers(pcsclite, async_result);
        }
    } else {
        /* Store the readers_name in the baton */
        async_result->readers_name = readers_name;
        async_result->readers_name_length = readers_name_length;
    }

    return result;
}
