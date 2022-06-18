/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2021-2022 KiCad Developers.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <atomic>
#include <thread>
#include <board.h>
#include <board_design_settings.h>
#include <connectivity/connectivity_data.h>
#include <zone.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_track.h>
#include <geometry/shape_line_chain.h>
#include <geometry/shape_poly_set.h>
#include <drc/drc_rule.h>
#include <drc/drc_item.h>
#include <drc/drc_test_provider.h>


/*
    This loads some rule resolvers for the ZONE_FILLER, and checks that pad thermal relief
    connections have at least the required number of spokes.

    Errors generated:
    - DRCE_STARVED_THERMAL
*/

class DRC_TEST_PROVIDER_ZONE_CONNECTIONS : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_ZONE_CONNECTIONS()
    {
    }

    virtual ~DRC_TEST_PROVIDER_ZONE_CONNECTIONS()
    {
    }

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return wxT( "zone connections" );
    };

    virtual const wxString GetDescription() const override
    {
        return wxT( "Checks thermal reliefs for a sufficient number of connecting spokes" );
    }

private:
    void testZoneLayer( ZONE* aZone, PCB_LAYER_ID aLayer );
};


void DRC_TEST_PROVIDER_ZONE_CONNECTIONS::testZoneLayer( ZONE* aZone, PCB_LAYER_ID aLayer )
{
    BOARD*                             board = m_drcEngine->GetBoard();
    BOARD_DESIGN_SETTINGS&             bds = board->GetDesignSettings();
    std::shared_ptr<CONNECTIVITY_DATA> connectivity = board->GetConnectivity();
    DRC_CONSTRAINT                     constraint;

    const std::shared_ptr<SHAPE_POLY_SET>& zoneFill = aZone->GetFilledPolysList( aLayer );

    for( FOOTPRINT* footprint : board->Footprints() )
    {
        for( PAD* pad : footprint->Pads() )
        {
            if( m_drcEngine->IsErrorLimitExceeded( DRCE_STARVED_THERMAL ) )
                return;

            if( m_drcEngine->IsCancelled() )
                return;

            // Quick tests for "connected":
            //
            if( !pad->FlashLayer( aLayer ) )
                continue;

            if( pad->GetNetCode() != aZone->GetNetCode() || pad->GetNetCode() <= 0 )
                continue;

            EDA_RECT item_boundingbox = pad->GetBoundingBox();

            if( !item_boundingbox.Intersects( aZone->GetCachedBoundingBox() ) )
                continue;

            // If those passed, do a thorough test:
            //
            constraint = bds.m_DRCEngine->EvalZoneConnection( pad, aZone, aLayer );
            ZONE_CONNECTION conn = constraint.m_ZoneConnection;

            if( conn != ZONE_CONNECTION::THERMAL )
                continue;

            constraint = bds.m_DRCEngine->EvalRules( MIN_RESOLVED_SPOKES_CONSTRAINT, pad, aZone,
                                                     aLayer );
            int minCount = constraint.m_Value.Min();

            if( constraint.GetSeverity() == RPT_SEVERITY_IGNORE || minCount <= 0 )
                continue;

            SHAPE_POLY_SET padPoly;
            pad->TransformShapeWithClearanceToPolygon( padPoly, aLayer, 0, ARC_LOW_DEF,
                                                       ERROR_OUTSIDE );

            SHAPE_LINE_CHAIN& padOutline = padPoly.Outline( 0 );
            std::vector<SHAPE_LINE_CHAIN::INTERSECTION> intersections;
            int spokes = 0;

            for( int jj = 0; jj < zoneFill->OutlineCount(); ++jj )
                padOutline.Intersect( zoneFill->Outline( jj ), intersections, true );

            spokes += intersections.size() / 2;

            if( spokes <= 0 )
                continue;

            // Now we know we're connected, so see if there are any other manual spokes
            // added:
            //
            for( PCB_TRACK* track : connectivity->GetConnectedTracks( pad ) )
            {
                if( padOutline.PointInside( track->GetStart() ) )
                {
                    if( aZone->GetFilledPolysList( aLayer )->Collide( track->GetEnd() ) )
                        spokes++;
                }
                else if( padOutline.PointInside( track->GetEnd() ) )
                {
                    if( aZone->GetFilledPolysList( aLayer )->Collide( track->GetStart() ) )
                        spokes++;
                }
            }

            // And finally report it if there aren't enough:
            //
            if( spokes < minCount )
            {
                std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_STARVED_THERMAL );
                wxString msg;

                msg.Printf( _( "(%s min spoke count %d; actual %d)" ),
                              constraint.GetName(),
                              minCount,
                              spokes );

                drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + msg );
                drce->SetItems( aZone, pad );
                drce->SetViolatingRule( constraint.GetParentRule() );

                reportViolation( drce, pad->GetPosition(), UNDEFINED_LAYER );
            }
        }
    }
}


bool DRC_TEST_PROVIDER_ZONE_CONNECTIONS::Run()
{
    BOARD*                             board = m_drcEngine->GetBoard();
    BOARD_DESIGN_SETTINGS&             bds = board->GetDesignSettings();
    std::shared_ptr<CONNECTIVITY_DATA> connectivity = board->GetConnectivity();
    DRC_CONSTRAINT                     constraint;

    if( !reportPhase( _( "Checking thermal reliefs..." ) ) )
        return false;   // DRC cancelled

    std::vector< std::pair<ZONE*, PCB_LAYER_ID> > zoneLayers;

    for( ZONE* zone : board->m_DRCCopperZones )
    {
        for( PCB_LAYER_ID layer : zone->GetLayerSet().Seq() )
            zoneLayers.push_back( { zone, layer } );
    }

    int                 zoneLayerCount = zoneLayers.size();
    std::atomic<size_t> next( 0 );
    std::atomic<size_t> done( 1 );
    std::atomic<size_t> threads_finished( 0 );
    size_t parallelThreadCount = std::max<size_t>( std::thread::hardware_concurrency(), 2 );

    for( size_t ii = 0; ii < parallelThreadCount; ++ii )
    {
        std::thread t = std::thread(
                [&]( )
                {
                    for( int i = next.fetch_add( 1 ); i < zoneLayerCount; i = next.fetch_add( 1 ) )
                    {
                        testZoneLayer( zoneLayers[i].first, zoneLayers[i].second );
                        done.fetch_add( 1 );

                        if( m_drcEngine->IsCancelled() )
                            break;
                    }

                    threads_finished.fetch_add( 1 );
                } );

        t.detach();
    }

    while( threads_finished < parallelThreadCount )
    {
        m_drcEngine->ReportProgress( (double) done / (double) zoneLayerCount );
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }

    return !m_drcEngine->IsCancelled();
}


namespace detail
{
static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_ZONE_CONNECTIONS> dummy;
}
