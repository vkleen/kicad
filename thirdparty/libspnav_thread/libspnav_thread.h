#ifndef LIBSPNAV_THREAD_H_
#define LIBSPNAV_THREAD_H_

#include <wx/event.h>
#include <functional>
#include <spnav.h>

void spnav_focus(wxEvtHandler *target, std::function<void(spnav_event)> cb);

#endif
