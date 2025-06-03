#pragma once

#include "../dshowcapture.hpp"
#include "dshow-base.hpp"

namespace DShow {
    struct DeviceDialogBox {
        DeviceDialogBox();
        ~DeviceDialogBox() = default;

        void Open(IUnknown* filter);
        void Close();
        DWORD Create();

        static DWORD WINAPI CallCreate(void* param) {
            DeviceDialogBox* obj = (DeviceDialogBox*) param;
            return obj->Create();
        }

        ComPtr<IUnknown> deviceFilter;
        DWORD threadId;
        HANDLE threadHandle;
        bool isOpen;
    };
};
