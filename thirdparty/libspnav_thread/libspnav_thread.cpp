#include "libspnav_thread.h"

#include <cstdint>

#include <thread>
#include <mutex>
#include <atomic>
#include <tuple>

#include <wx/log.h>

#include <spnav.h>

class scope_guard {
public:
  template<class CB>
  scope_guard(CB&& cb) try : f(std::forward<CB>(cb)) {
  } catch(...) {
    cb();
    throw;
  }

  scope_guard(scope_guard&& other) : f(std::move(other.f)) {
    other.f = nullptr;
  }

  ~scope_guard() {
    if(f) f();
  };

  void dismiss() noexcept {
    f = nullptr;
  }

  scope_guard(const scope_guard&) = delete;
  void operator=(const scope_guard&) = delete;
private:
  std::function<void()> f;
};

static class cb_data {
public:
  cb_data() : lock{}, target{nullptr}, cb{nullptr} {}

  auto get() {
    std::lock_guard guard(lock);
    return std::make_tuple(target, cb);
  }

  void set(wxEvtHandler *target, std::function<void(spnav_event)> cb) {
    std::lock_guard guard(lock);
    this->target = target;
    this->cb = cb;
  }
private:
  std::mutex lock;
  wxEvtHandler *target;
  std::function<void(spnav_event)> cb;
} spnav_data;
std::atomic_flag spnav_thread_running;

static void spnav_thread() {
  scope_guard _{[](){
    spnav_close();
    spnav_thread_running.clear();
  }};

  if (spnav_open() == -1) {
    return;
  }

  spnav_event ev;
  while(spnav_wait_event(&ev)) {
    auto [t, cb] = spnav_data.get();

    if (t && cb)
      t->CallAfter([cb, ev](){
        cb(ev);
      });
  }
}

void spnav_focus(wxEvtHandler *target, std::function<void(spnav_event)> cb) {
  if (!spnav_thread_running.test_and_set()) {
    std::thread{spnav_thread}.detach();
  }
  spnav_data.set(target, cb);
}
