/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2020 Roberto Fernandez Bautista <@Qbort>
 * Copyright (C) 2020 KiCad Developers, see AUTHORS.txt for contributors.
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

/**
 * @file cadstar_pcb.h
 * @brief Converts a CADSTAR_PCB_ARCHIVE_PARSER object into a KiCad BOARD object
 */

#ifndef CADSTAR_PCB_H_
#define CADSTAR_PCB_H_

#include <cadstar_pcb_archive_parser.h>

class BOARD;

class CADSTAR_PCB : public CADSTAR_PCB_ARCHIVE_PARSER
{
public:
    explicit CADSTAR_PCB( wxString aFilename ) : CADSTAR_PCB_ARCHIVE_PARSER( aFilename )
    {
    }

    /**
     * @brief Loads a CADSTAR PCB Archive file into the KiCad BOARD object given
     * @param aBoard 
     */
    void Load( ::BOARD* aBoard );

private:
    ::BOARD*                              mBoard;
    std::map<LAYER_ID, PCB_LAYER_ID>      mLayermap; //<Map between Cadstar and KiCad Layers
    std::map<PHYSICAL_LAYER_ID, LAYER_ID> mCopperLayers;
    void                                  loadBoardStackup();
    PCB_LAYER_ID                          getKiCadCopperLayerID( unsigned int aLayerNum );
};


#endif // CADSTAR_PCB_H_
