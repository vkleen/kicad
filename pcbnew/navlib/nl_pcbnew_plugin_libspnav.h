#ifndef NL_PCBNEW_PLUGIN_LIBSPNAV_H_
#define NL_PCBNEW_PLUGIN_LIBSPNAV_H_

class PCB_DRAW_PANEL_GAL;
namespace KIGFX
{
class PCB_VIEW;
}

class NL_PCBNEW_PLUGIN_LIBSPNAV
{
  public:
    NL_PCBNEW_PLUGIN_LIBSPNAV(PCB_DRAW_PANEL_GAL* vieport);
    ~NL_PCBNEW_PLUGIN_LIBSPNAV();

    void SetFocus(bool focus);
  private:
    PCB_DRAW_PANEL_GAL* m_viewport2D;
    KIGFX::PCB_VIEW* m_view;
};

#endif
