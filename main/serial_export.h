#pragma once

namespace serial_export {
bool init();
// Called only by the UI task: storage remains exclusively owned by that task.
bool requested();
void send_documents(const char* root, bool saved);
}
