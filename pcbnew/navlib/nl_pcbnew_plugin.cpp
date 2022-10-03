/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2021 3Dconnexion
 * Copyright (C) 2021 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "nl_pcbnew_plugin.h"
#if defined( KICAD_USE_3DCONNEXION )

#if defined( KICAD_3DCONNEXION_LIBSPNAV )
#include "nl_pcbnew_plugin_libspnav.h"

NL_PCBNEW_PLUGIN::NL_PCBNEW_PLUGIN( PCB_DRAW_PANEL_GAL* aViewport ) :
  m_impl(new NL_PCBNEW_PLUGIN_LIBSPNAV(aViewport))
{
}
#else
#include "nl_pcbnew_plugin_impl.h"


NL_PCBNEW_PLUGIN::NL_PCBNEW_PLUGIN( PCB_DRAW_PANEL_GAL* aViewport ) :
        m_impl( new NL_PCBNEW_PLUGIN_IMPL( aViewport ) )
{
}
#endif


NL_PCBNEW_PLUGIN::~NL_PCBNEW_PLUGIN()
{
    delete m_impl;
}


void NL_PCBNEW_PLUGIN::SetFocus( bool focus )
{
    m_impl->SetFocus( focus );
}
#else


NL_PCBNEW_PLUGIN::NL_PCBNEW_PLUGIN( PCB_DRAW_PANEL_GAL* aViewport )
{
}


void NL_PCBNEW_PLUGIN::SetFocus( bool focus )
{
}


NL_PCBNEW_PLUGIN::~NL_PCBNEW_PLUGIN()
{
}
#endif
