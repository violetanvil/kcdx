#pragma once
#include "kcdx/Interfaces.h"

namespace kcdx::messaging {

// Get the published kcdxMessagingInterface implementation. Returned pointer
// is owned by the engine and remains valid for the lifetime of the process.
// Used by interfaces.cpp's QueryInterface dispatch.
const kcdxMessagingInterface* GetInterface();

// Engine-side helper: fire a lifecycle message with sender = null. Convenience
// wrapper over Dispatch that doesn't require a handle, used by the engine
// itself (plugin loader fires kcdxMessage_PostLoad/PostPostLoad, hooks fire
// kcdxMessage_InputLoaded, etc.).
void FireEngineMessage(uint32_t messageType,
                       const void* data = nullptr,
                       uint32_t dataLen = 0);

}  // namespace kcdx::messaging
