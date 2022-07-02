#include "nl_pcbnew_plugin_libspnav.h"

#include <math/vector2d.h>

#include <pcb_draw_panel_gal.h>

#include <libspnav_thread.h>
#include <spnav.h>

NL_PCBNEW_PLUGIN_LIBSPNAV::NL_PCBNEW_PLUGIN_LIBSPNAV(PCB_DRAW_PANEL_GAL *viewport) :
  m_viewport2D(viewport), m_view(m_viewport2D->GetView())
{
}

NL_PCBNEW_PLUGIN_LIBSPNAV::~NL_PCBNEW_PLUGIN_LIBSPNAV()
{
}

void NL_PCBNEW_PLUGIN_LIBSPNAV::SetFocus(bool focus)
{
  spnav_focus(focus ? m_viewport2D->GetParent() : nullptr, [view{m_view}, viewport{m_viewport2D}](spnav_event ev) {
    const double scale = 1e-1;
    const double zoom_scale = 1e-4;
    switch(ev.type) {
      case SPNAV_EVENT_MOTION: {
        VECTOR2D delta = view->ToWorld({ev.motion.x * scale, -ev.motion.z * scale}, false);
        view->SetCenter(view->GetCenter() - delta);

        double zoom = ev.motion.y > 0 ? 1 + ev.motion.y*zoom_scale : 1/(1 - ev.motion.y*zoom_scale);
        view->SetScale(view->GetScale() * zoom);

        viewport->Refresh();
        break;
      }
      default:
        break;
    }
  });
}
