#ifndef NL_PCBNEW_PLUGIN_LIBSPNAV_H_
#define NL_PCBNEW_PLUGIN_LIBSPNAV_H_

class EDA_DRAW_PANEL_GAL;
namespace KIGFX
{
class SCH_VIEW;
}

class NL_SCHEMATIC_PLUGIN_LIBSPNAV
{
  public:
    NL_SCHEMATIC_PLUGIN_LIBSPNAV();
    ~NL_SCHEMATIC_PLUGIN_LIBSPNAV();

    void SetCanvas(EDA_DRAW_PANEL_GAL* viewport);
    void SetFocus(bool focus);
  private:
    EDA_DRAW_PANEL_GAL* m_viewport2D;
    KIGFX::SCH_VIEW* m_view;
};

#endif
