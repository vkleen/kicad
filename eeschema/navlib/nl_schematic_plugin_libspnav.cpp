#include "nl_schematic_plugin_libspnav.h"

#include <math/vector2d.h>

#include <sch_base_frame.h>
#include <class_draw_panel_gal.h>

#include <libspnav_thread.h>
#include <spnav.h>

NL_SCHEMATIC_PLUGIN_LIBSPNAV::NL_SCHEMATIC_PLUGIN_LIBSPNAV() :
  m_viewport2D{nullptr}, m_view{nullptr}
{
}

NL_SCHEMATIC_PLUGIN_LIBSPNAV::~NL_SCHEMATIC_PLUGIN_LIBSPNAV()
{
}

void NL_SCHEMATIC_PLUGIN_LIBSPNAV::SetCanvas(EDA_DRAW_PANEL_GAL* viewport)
{
  m_viewport2D = viewport;
  if (m_viewport2D)
    m_view = static_cast<KIGFX::SCH_VIEW*>(m_viewport2D->GetView());
}

void NL_SCHEMATIC_PLUGIN_LIBSPNAV::SetFocus(bool focus)
{
  if (!m_viewport2D) return;

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
