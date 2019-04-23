/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2018 CERN
 * @author Jon Evans <jon@craftyjon.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <list>
#include <thread>
#include <algorithm>
#include <future>
#include <vector>
#include <unordered_map>
#include <profile.h>

#include <common.h>
#include <erc.h>
#include <sch_edit_frame.h>
#include <sch_bus_entry.h>
#include <sch_component.h>
#include <sch_line.h>
#include <sch_pin.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <sch_sheet_path.h>
#include <sch_text.h>

#include <connection_graph.h>


bool CONNECTION_SUBGRAPH::ResolveDrivers( bool aCreateMarkers )
{
    int highest_priority = -1;
    std::vector<SCH_ITEM*> candidates;
    std::vector<SCH_ITEM*> strong_drivers;

    m_driver = nullptr;

    // Hierarchical labels are lower priority than local labels here,
    // because on the first pass we want local labels to drive subgraphs
    // so that we can identify same-sheet neighbors and link them together.
    // Hierarchical labels will end up overriding the final net name if
    // a higher-level sheet has a different name during the hierarchical
    // pass.

    for( auto item : m_drivers )
    {
        int item_priority = 0;

        switch( item->Type() )
        {
        case SCH_SHEET_PIN_T:           item_priority = 2; break;
        case SCH_HIER_LABEL_T:  item_priority = 3; break;
        case SCH_LABEL_T:               item_priority = 4; break;
        case SCH_PIN_T:
        {
            auto sch_pin = static_cast<SCH_PIN*>( item );

            if( sch_pin->IsPowerConnection() )
                item_priority = 5;
            else
                item_priority = 1;

            // Skip power flags, etc
            if( item_priority == 1 && !sch_pin->GetParentComponent()->IsInNetlist() )
                continue;

            break;
        }
        case SCH_GLOBAL_LABEL_T:        item_priority = 6; break;
        default: break;
        }

        if( item_priority >= 3 )
            strong_drivers.push_back( item );

        if( item_priority > highest_priority )
        {
            candidates.clear();
            candidates.push_back( item );
            highest_priority = item_priority;
        }
        else if( candidates.size() && ( item_priority == highest_priority ) )
        {
            candidates.push_back( item );
        }
    }

    if( highest_priority >= 3 )
        m_strong_driver = true;

    // Power pins are 5, global labels are 6
    m_local_driver = ( highest_priority < 5 );

    if( candidates.size() )
    {
        if( candidates.size() > 1 )
        {
            if( highest_priority == 1 || highest_priority == 5 )
            {
                // We have multiple options and they are all component pins.
                std::sort( candidates.begin(), candidates.end(),
                           [this]( SCH_ITEM* a, SCH_ITEM* b) -> bool
                            {
                                auto pin_a = static_cast<SCH_PIN*>( a );
                                auto pin_b = static_cast<SCH_PIN*>( b );

                                auto name_a = pin_a->GetDefaultNetName( m_sheet );
                                auto name_b = pin_b->GetDefaultNetName( m_sheet );

                                return name_a < name_b;
                            } );
            }

            if( highest_priority == 2 )
            {
                // We have multiple options, and they are all hierarchical
                // sheet pins.  Let's prefer outputs over inputs.

                for( auto c : candidates )
                {
                    auto p = static_cast<SCH_SHEET_PIN*>( c );

                    if( p->GetShape() == NET_OUTPUT )
                    {
                        m_driver = c;
                        break;
                    }
                }
            }
        }

        if( !m_driver )
            m_driver = candidates[0];
    }

    if( strong_drivers.size() > 1 )
        m_multiple_drivers = true;

    // Drop weak drivers
    if( m_strong_driver )
        m_drivers = strong_drivers;

    // Cache driver connection
    if( m_driver )
        m_driver_connection = m_driver->Connection( m_sheet );
    else
        m_driver_connection = nullptr;

    if( aCreateMarkers && m_multiple_drivers )
    {
        // First check if all the candidates are actually the same
        bool same = true;
        auto first = GetNameForDriver( candidates[0] );

        for( unsigned i = 1; i < candidates.size(); i++ )
        {
            if( GetNameForDriver( candidates[i] ) != first )
            {
                same = false;
                break;
            }
        }

        if( !same )
        {
            wxString msg;
            msg.Printf( _( "%s and %s are both attached to the same wires. "
                           "%s was picked as the label to use for netlisting." ),
                        candidates[0]->GetSelectMenuText( m_frame->GetUserUnits() ),
                        candidates[1]->GetSelectMenuText( m_frame->GetUserUnits() ),
                        candidates[0]->Connection( m_sheet )->Name() );

            wxASSERT( candidates[0] != candidates[1] );

            auto p0 = ( candidates[0]->Type() == SCH_PIN_T ) ?
                      static_cast<SCH_PIN*>( candidates[0] )->GetTransformedPosition() :
                      candidates[0]->GetPosition();

            auto p1 = ( candidates[1]->Type() == SCH_PIN_T ) ?
                      static_cast<SCH_PIN*>( candidates[1] )->GetTransformedPosition() :
                      candidates[1]->GetPosition();

            auto marker = new SCH_MARKER();
            marker->SetTimeStamp( GetNewTimeStamp() );
            marker->SetMarkerType( MARKER_BASE::MARKER_ERC );
            marker->SetErrorLevel( MARKER_BASE::MARKER_SEVERITY_WARNING );
            marker->SetData( ERCE_DRIVER_CONFLICT, p0, msg, p1 );

            m_sheet.LastScreen()->Append( marker );

            // If aCreateMarkers is true, then this is part of ERC check, so we
            // should return false even if the driver was assigned
            return false;
        }
    }

    return aCreateMarkers || ( m_driver != nullptr );
}


wxString CONNECTION_SUBGRAPH::GetNetName() const
{
    if( !m_driver || m_dirty )
        return "";

    if( !m_driver->Connection( m_sheet ) )
    {
        #ifdef CONNECTIVITY_DEBUG
        wxASSERT_MSG( false, "Tried to get the net name of an item with no connection" );
        #endif

        return "";
    }

    return m_driver->Connection( m_sheet )->Name();
}


std::vector<SCH_ITEM*> CONNECTION_SUBGRAPH::GetBusLabels() const
{
    std::vector<SCH_ITEM*> labels;

    for( auto item : m_drivers )
    {
        switch( item->Type() )
        {
        case SCH_LABEL_T:
        case SCH_GLOBAL_LABEL_T:
        {
            auto label_conn = item->Connection( m_sheet );

            // Only consider bus vectors
            if( label_conn->Type() == CONNECTION_BUS )
                labels.push_back( item );
        }
        default: break;
        }
    }

    return labels;
}


wxString CONNECTION_SUBGRAPH::GetNameForDriver( SCH_ITEM* aItem ) const
{
    wxString name;

    switch( aItem->Type() )
    {
    case SCH_PIN_T:
    {
        auto power_object = static_cast<SCH_PIN*>( aItem );
        name = power_object->GetDefaultNetName( m_sheet );
        break;
    }

    case SCH_LABEL_T:
    case SCH_GLOBAL_LABEL_T:
    case SCH_HIER_LABEL_T:
    case SCH_SHEET_PIN_T:
    {
        name = static_cast<SCH_TEXT*>( aItem )->GetText();
        break;
    }

    default:
        break;
    }

    return name;
};


void CONNECTION_SUBGRAPH::Absorb( CONNECTION_SUBGRAPH* aOther )
{
    wxASSERT( m_sheet == aOther->m_sheet );

    for( SCH_ITEM* item : aOther->m_items )
    {
        item->Connection( m_sheet )->SetSubgraphCode( m_code );
        AddItem( item );
    }

    m_bus_neighbors.insert( aOther->m_bus_neighbors.begin(), aOther->m_bus_neighbors.end() );

    aOther->m_absorbed = true;
    aOther->m_dirty = false;
    aOther->m_driver = nullptr;
    aOther->m_driver_connection = nullptr;
}


void CONNECTION_SUBGRAPH::AddItem( SCH_ITEM* aItem )
{
    m_items.push_back( aItem );

    if( aItem->Connection( m_sheet )->IsDriver() )
        m_drivers.push_back( aItem );

    if( aItem->Type() == SCH_SHEET_PIN_T )
        m_hier_pins.push_back( static_cast<SCH_SHEET_PIN*>( aItem ) );
    else if( aItem->Type() == SCH_HIER_LABEL_T )
        m_hier_ports.push_back( static_cast<SCH_HIERLABEL*>( aItem ) );
}


void CONNECTION_SUBGRAPH::UpdateItemConnections()
{
    if( !m_driver_connection )
        return;

    for( auto item : m_items )
    {
        auto item_conn = item->Connection( m_sheet );

        if( !item_conn )
            item_conn = item->InitializeConnection( m_sheet );

        if( ( m_driver_connection->IsBus() && item_conn->IsNet() ) ||
            ( m_driver_connection->IsNet() && item_conn->IsBus() ) )
        {
            continue;
        }

        if( item != m_driver )
        {
            item_conn->Clone( *m_driver_connection );
            item_conn->ClearDirty();
        }
    }
}


void CONNECTION_GRAPH::Reset()
{
    for( auto subgraph : m_subgraphs )
        delete subgraph;

    m_items.clear();
    m_subgraphs.clear();
    m_invisible_power_pins.clear();
    m_bus_alias_cache.clear();
    m_net_name_to_code_map.clear();
    m_bus_name_to_code_map.clear();
    m_net_code_to_subgraphs_map.clear();
    m_last_net_code = 1;
    m_last_bus_code = 1;
    m_last_subgraph_code = 1;
}


void CONNECTION_GRAPH::Recalculate( SCH_SHEET_LIST aSheetList, bool aUnconditional )
{
    PROF_COUNTER phase1;

    if( aUnconditional )
        Reset();

    for( const auto& sheet : aSheetList )
    {
        std::vector<SCH_ITEM*> items;

        for( auto item = sheet.LastScreen()->GetDrawItems();
             item; item = item->Next() )
        {
            if( item->IsConnectable() &&
                ( aUnconditional || item->IsConnectivityDirty() ) )
            {
                items.push_back( item );
            }
        }

        updateItemConnectivity( sheet, items );
    }

    phase1.Stop();
    wxLogTrace( "CONN_PROFILE", "UpdateItemConnectivity() %0.4f ms", phase1.msecs() );

    PROF_COUNTER tde;

    // IsDanglingStateChanged() also adds connected items for things like SCH_TEXT
    SCH_SCREENS schematic;
    schematic.TestDanglingEnds();

    tde.Stop();
    wxLogTrace( "CONN_PROFILE", "TestDanglingEnds() %0.4f ms", tde.msecs() );

    buildConnectionGraph();
}


void CONNECTION_GRAPH::updateItemConnectivity( SCH_SHEET_PATH aSheet,
                                               std::vector<SCH_ITEM*> aItemList )
{
    std::unordered_map< wxPoint, std::vector<SCH_ITEM*> > connection_map;

    for( auto item : aItemList )
    {
        std::vector< wxPoint > points;
        item->GetConnectionPoints( points );
        item->ConnectedItems().clear();

        if( item->Type() == SCH_SHEET_T )
        {
            for( auto& pin : static_cast<SCH_SHEET*>( item )->GetPins() )
            {
                if( !pin.Connection( aSheet ) )
                {
                    pin.InitializeConnection( aSheet );
                }

                pin.ConnectedItems().clear();
                pin.Connection( aSheet )->Reset();

                connection_map[ pin.GetTextPos() ].push_back( &pin );
                m_items.insert( &pin );
            }
        }
        else if( item->Type() == SCH_COMPONENT_T )
        {
            SCH_COMPONENT* component = static_cast<SCH_COMPONENT*>( item );
            TRANSFORM t = component->GetTransform();

            // Assumption: we don't need to call UpdatePins() here because anything
            // that would change the pins of the component will have called it already

            for( SCH_PIN& pin : component->GetPins() )
            {
                pin.InitializeConnection( aSheet );

                wxPoint pos = t.TransformCoordinate( pin.GetPosition() ) + component->GetPosition();

                // because calling the first time is not thread-safe
                pin.GetDefaultNetName( aSheet );
                pin.ConnectedItems().clear();

                // Invisible power pins need to be post-processed later

                if( pin.IsPowerConnection() && !pin.IsVisible() )
                    m_invisible_power_pins.push_back( std::make_pair( aSheet, &pin ) );

                connection_map[ pos ].push_back( &pin );
                m_items.insert( &pin );
            }
        }
        else
        {
            m_items.insert( item );
            auto conn = item->InitializeConnection( aSheet );

            // Set bus/net property here so that the propagation code uses it
            switch( item->Type() )
            {
            case SCH_LINE_T:
                conn->SetType( item->GetLayer() == LAYER_BUS ? CONNECTION_BUS : CONNECTION_NET );
                break;

            case SCH_BUS_BUS_ENTRY_T:
                conn->SetType( CONNECTION_BUS );
                break;

            case SCH_PIN_T:
            case SCH_BUS_WIRE_ENTRY_T:
                conn->SetType( CONNECTION_NET );
                break;

            default:
                break;
            }

            for( auto point : points )
            {
                connection_map[ point ].push_back( item );
            }
        }

        item->SetConnectivityDirty( false );
    }

    for( const auto& it : connection_map )
    {
        auto connection_vec = it.second;
        SCH_ITEM* junction = nullptr;

        for( auto primary_it = connection_vec.begin(); primary_it != connection_vec.end(); primary_it++ )
        {
            auto connected_item = *primary_it;

            // Look for junctions.  For points that have a junction, we want all
            // items to connect to the junction but not to each other.

            if( connected_item->Type() == SCH_JUNCTION_T )
            {
                junction = connected_item;
            }

            // Bus entries are special: they can have connection points in the
            // middle of a wire segment, because the junction algo doesn't split
            // the segment in two where you place a bus entry.  This means that
            // bus entries that don't land on the end of a line segment need to
            // have "virtual" connection points to the segments they graphically
            // touch.
            if( connected_item->Type() == SCH_BUS_WIRE_ENTRY_T )
            {
                // If this location only has the connection point of the bus
                // entry itself, this means that either the bus entry is not
                // connected to anything graphically, or that it is connected to
                // a segment at some point other than at one of the endpoints.
                if( connection_vec.size() == 1 )
                {
                    auto screen = aSheet.LastScreen();
                    auto bus = screen->GetBus( it.first );

                    if( bus )
                    {
                        auto bus_entry = static_cast<SCH_BUS_WIRE_ENTRY*>( connected_item );
                        bus_entry->m_connected_bus_item = bus;
                    }
                }
            }

            // Bus-to-bus entries are treated just like bus wires
            if( connected_item->Type() == SCH_BUS_BUS_ENTRY_T )
            {
                if( connection_vec.size() < 2 )
                {
                    auto screen = aSheet.LastScreen();
                    auto bus = screen->GetBus( it.first );

                    if( bus )
                    {
                        auto bus_entry = static_cast<SCH_BUS_BUS_ENTRY*>( connected_item );

                        if( it.first == bus_entry->GetPosition() )
                            bus_entry->m_connected_bus_items[0] = bus;
                        else
                            bus_entry->m_connected_bus_items[1] = bus;

                        bus_entry->ConnectedItems().insert( bus );
                        bus->ConnectedItems().insert( bus_entry );
                    }
                }
            }

            for( auto test_it = primary_it + 1; test_it != connection_vec.end(); test_it++ )
            {
                auto test_item = *test_it;

                if( !junction && test_item->Type() == SCH_JUNCTION_T )
                {
                    junction = test_item;
                }

                if( connected_item != test_item &&
                    connected_item != junction &&
                    connected_item->ConnectionPropagatesTo( test_item ) &&
                    test_item->ConnectionPropagatesTo( connected_item ) )
                {
                    connected_item->ConnectedItems().insert( test_item );
                    test_item->ConnectedItems().insert( connected_item );
                }

                // Set up the link between the bus entry net and the bus
                if( connected_item->Type() == SCH_BUS_WIRE_ENTRY_T )
                {
                    if( test_item->Connection( aSheet )->IsBus() )
                    {
                        auto bus_entry = static_cast<SCH_BUS_WIRE_ENTRY*>( connected_item );
                        bus_entry->m_connected_bus_item = test_item;
                    }
                }
            }

            // If we got this far and did not find a connected bus item for a bus entry,
            // we should do a manual scan in case there is a bus item on this connection
            // point but we didn't pick it up earlier because there is *also* a net item here.
            if( connected_item->Type() == SCH_BUS_WIRE_ENTRY_T )
            {
                auto bus_entry = static_cast<SCH_BUS_WIRE_ENTRY*>( connected_item );

                if( !bus_entry->m_connected_bus_item )
                {
                    auto screen = aSheet.LastScreen();
                    auto bus = screen->GetBus( it.first );

                    if( bus )
                        bus_entry->m_connected_bus_item = bus;
                }
            }
        }
    }
}


// TODO(JE) This won't give the same subgraph IDs (and eventually net/graph codes)
// to the same subgraph necessarily if it runs over and over again on the same
// sheet.  We need:
//
//  a) a cache of net/bus codes, like used before
//  b) to persist the CONNECTION_GRAPH globally so the cache is persistent,
//  c) some way of trying to avoid changing net names.  so we should keep track
//     of the previous driver of a net, and if it comes down to choosing between
//     equally-prioritized drivers, choose the one that already exists as a driver
//     on some portion of the items.


void CONNECTION_GRAPH::buildConnectionGraph()
{
    PROF_COUNTER phase2;

    std::vector<CONNECTION_SUBGRAPH*> driver_subgraphs;
    // Recache all bus aliases for later use

    SCH_SHEET_LIST all_sheets( g_RootSheet );

    for( unsigned i = 0; i < all_sheets.size(); i++ )
    {
        for( auto alias : all_sheets[i].LastScreen()->GetBusAliases() )
        {
            m_bus_alias_cache[ alias->GetName() ] = alias;
        }
    }

    // Build subgraphs from items (on a per-sheet basis)

    for( SCH_ITEM* item : m_items )
    {
        for( const auto& it : item->m_connection_map )
        {
            const auto sheet = it.first;
            auto connection = it.second;

            if( connection->SubgraphCode() == 0 )
            {
                auto subgraph = new CONNECTION_SUBGRAPH( m_frame );

                subgraph->m_code = m_last_subgraph_code++;
                subgraph->m_sheet = sheet;

                subgraph->AddItem( item );

                connection->SetSubgraphCode( subgraph->m_code );

                std::list<SCH_ITEM*> members;

                auto get_items = [ &sheet ] ( SCH_ITEM* aItem ) -> bool
                    {
                      auto* conn = aItem->Connection( sheet );

                      if( !conn )
                          conn = aItem->InitializeConnection( sheet );

                      return ( conn->SubgraphCode() == 0 );
                    };

                std::copy_if( item->ConnectedItems().begin(),
                              item->ConnectedItems().end(),
                              std::back_inserter( members ), get_items );

                for( auto connected_item : members )
                {
                    if( connected_item->Type() == SCH_NO_CONNECT_T )
                        subgraph->m_no_connect = connected_item;

                    auto connected_conn = connected_item->Connection( sheet );

                    wxASSERT( connected_conn );

                    if( connected_conn->SubgraphCode() == 0 )
                    {
                        connected_conn->SetSubgraphCode( subgraph->m_code );
                        subgraph->AddItem( connected_item);

                        std::copy_if( connected_item->ConnectedItems().begin(),
                                      connected_item->ConnectedItems().end(),
                                      std::back_inserter( members ), get_items );
                    }
                }

                subgraph->m_dirty = true;
                m_subgraphs.push_back( subgraph );
            }
        }
    }

    /**
     * TODO(JE)
     *
     * It would be good if net codes were preserved as much as possible when
     * generating netlists, so that unnamed nets don't keep shifting around when
     * you regenerate.
     *
     * Right now, we are clearing out the old connections up in
     * UpdateItemConnectivity(), but that is useful information, so maybe we
     * need to just set the dirty flag or something.
     *
     * That way, ResolveDrivers() can check what the driver of the subgraph was
     * previously, and if it is in the situation of choosing between equal
     * candidates for an auto-generated net name, pick the previous one.
     *
     * N.B. the old algorithm solves this by sorting the possible net names
     * alphabetically, so as long as the same refdes components are involved,
     * the net will be the same.
     */

    // Resolve drivers for subgraphs and propagate connectivity info

    // We don't want to spin up a new thread for fewer than 8 nets (overhead costs)
    size_t parallelThreadCount = std::min<size_t>( std::thread::hardware_concurrency(),
            ( m_subgraphs.size() + 3 ) / 4 );

    std::atomic<size_t> nextSubgraph( 0 );
    std::vector<std::future<size_t>> returns( parallelThreadCount );
    std::vector<CONNECTION_SUBGRAPH*> dirty_graphs;

    std::copy_if( m_subgraphs.begin(), m_subgraphs.end(), std::back_inserter( dirty_graphs ),
                  [&] ( const CONNECTION_SUBGRAPH* candidate ) {
                      return candidate->m_dirty;
                  } );

    auto update_lambda = [&nextSubgraph, &dirty_graphs]() -> size_t
    {
        for( size_t subgraphId = nextSubgraph++; subgraphId < dirty_graphs.size(); subgraphId = nextSubgraph++ )
        {
            auto subgraph = dirty_graphs[subgraphId];

            if( !subgraph->m_dirty )
                continue;

            // Special processing for some items
            for( auto item : subgraph->m_items )
            {
                switch( item->Type() )
                {
                case SCH_NO_CONNECT_T:
                    subgraph->m_no_connect = item;
                    break;

                case SCH_BUS_WIRE_ENTRY_T:
                    subgraph->m_bus_entry = item;
                    break;

                case SCH_PIN_T:
                {
                    auto pin = static_cast<SCH_PIN*>( item );

                    if( pin->GetType() == PIN_NC )
                        subgraph->m_no_connect = item;

                    break;
                }

                default:
                    break;
                }
            }

            if( !subgraph->ResolveDrivers() )
            {
                subgraph->m_dirty = false;
            }
            else
            {
                // Now the subgraph has only one driver
                auto driver = subgraph->m_driver;
                auto sheet = subgraph->m_sheet;
                auto connection = driver->Connection( sheet );

                // TODO(JE) This should live in SCH_CONNECTION probably
                switch( driver->Type() )
                {
                case SCH_LABEL_T:
                case SCH_GLOBAL_LABEL_T:
                case SCH_HIER_LABEL_T:
                {
                    auto text = static_cast<SCH_TEXT*>( driver );
                    connection->ConfigureFromLabel( text->GetShownText() );
                    break;
                }
                case SCH_SHEET_PIN_T:
                {
                    auto pin = static_cast<SCH_SHEET_PIN*>( driver );
                    auto txt = pin->GetShownText();

                    // TODO(JE) we need to check and deal with duplicates if we have more than one
                    // subgraph driven by different sheet pins with the same name.  We can detect
                    // these because sheet pins are weak drivers, so we can just scan for weak
                    // drivers with duplicate names

                    connection->ConfigureFromLabel( txt );
                    break;
                }
                case SCH_PIN_T:
                {
                    auto pin = static_cast<SCH_PIN*>( driver );
                    // NOTE(JE) GetDefaultNetName is not thread-safe.
                    connection->ConfigureFromLabel( pin->GetDefaultNetName( sheet ) );

                    break;
                }
                default:
                    wxLogTrace( "CONN", "Driver type unsupported: %s",
                                driver->GetSelectMenuText( MILLIMETRES ) );
                    break;
                }

                connection->SetDriver( driver );
                connection->ClearDirty();

                subgraph->m_dirty = false;
            }
        }

        return 1;
    };

    if( parallelThreadCount == 1 )
        update_lambda();
    else
    {
        for( size_t ii = 0; ii < parallelThreadCount; ++ii )
            returns[ii] = std::async( std::launch::async, update_lambda );

        // Finalize the threads
        for( size_t ii = 0; ii < parallelThreadCount; ++ii )
            returns[ii].wait();
    }

    // TODO(JE) The below loop may not be needed anymore
    // TODO(JE) are the label caches even needed anymore?
    // Check for subgraphs with the same net name but only weak drivers.
    // For example, two wires that are both connected to hierarchical
    // sheet pins that happen to have the same name, but are not the same.

    for( auto&& subgraph : m_subgraphs )
    {
        if( subgraph->m_strong_driver )
        {
            subgraph->m_dirty = true;
            // Add strong drivers to the cache, for later checking against conflicts

            auto driver = subgraph->m_driver;
            auto conn = subgraph->m_driver_connection;
            auto sheet = subgraph->m_sheet;
            auto name = conn->Name( true );

            switch( driver->Type() )
            {
            case SCH_LABEL_T:
            case SCH_HIER_LABEL_T:
            {
                m_local_label_cache[std::make_pair( sheet, name )].push_back( subgraph );
                break;
            }
            case SCH_GLOBAL_LABEL_T:
            {
                m_global_label_cache[name].push_back( subgraph );
                break;
            }
            case SCH_PIN_T:
            {
                auto pin = static_cast<SCH_PIN*>( driver );
                wxASSERT( pin->IsPowerConnection() );
                m_global_label_cache[name].push_back( subgraph );
                break;
            }
            default:
                wxLogTrace( "CONN", "Unexpected strong driver %s",
                            driver->GetSelectMenuText( MILLIMETRES ) );
                break;
            }
        }
    }

    std::copy_if( m_subgraphs.begin(), m_subgraphs.end(), std::back_inserter( driver_subgraphs ),
                  [&] ( const CONNECTION_SUBGRAPH* candidate ) -> bool {
                      return candidate->m_driver;
                  } );

    // Test subgraphs for net name conflicts against higher priority subgraphs
    // Suffix is a global increment to make things simpler, that way if we have
    // multiple instances of the same name that needs to get renamed, they will
    // definitely get unique names.  While this will potentially lead to some
    // confusing net names, this is really a corner case and won't happen if
    // users follow best practices to label their nets.
    unsigned suffix = 1;

    for( auto subgraph_it = driver_subgraphs.begin();
         subgraph_it != driver_subgraphs.end(); subgraph_it++ )
    {
        auto subgraph = *subgraph_it;

        if( !subgraph->m_dirty )
            continue;

        subgraph->m_dirty = false;

        if( subgraph->m_strong_driver )
            continue;

        auto conn = subgraph->m_driver_connection;
        auto name = conn->Name();
        auto local_name = conn->Name( true );

        // First check the caches
        if( m_global_label_cache.count( name )  ||
                ( m_local_label_cache.count( std::make_pair( subgraph->m_sheet, local_name ) ) ) )
        {
            auto new_name = wxString::Format( "%s%u", name, suffix );

            wxLogTrace( "CONN", "Subgraph %ld default name %s conflicts with a label. Changing to %s.",
                        subgraph->m_code, name, new_name );

            conn->SetSuffix( wxString::Format( "%u", suffix ) );
            suffix++;
            name = new_name;
        }

        for( auto candidate_it = subgraph_it + 1;
             candidate_it != driver_subgraphs.end(); candidate_it++ )
        {
            auto candidate = *candidate_it;

            if( !candidate->m_dirty )
                continue;

            if( candidate == subgraph || candidate->m_strong_driver )
                continue;

            if( candidate->m_sheet != subgraph->m_sheet )
                continue;

            auto c_conn = candidate->m_driver_connection;
            auto check_name = c_conn->Name();

            if( check_name == name )
            {
                auto new_name = wxString::Format( "%s%u", name, suffix );

                wxLogTrace( "CONN", "Subgraph %ld and %ld both have name %s. Changing %ld to %s.",
                            subgraph->m_code, candidate->m_code, name,
                            candidate->m_code, new_name );

                c_conn->SetSuffix( wxString::Format( "%u", suffix ) );

                candidate->m_dirty = false;
                suffix++;
            }
        }
    }

    // Generate subgraphs for invisible power pins.  These will be merged with other subgraphs
    // on the same sheet in the next loop.

    std::unordered_map<int, CONNECTION_SUBGRAPH*> invisible_pin_subgraphs;

    for( const auto& it : m_invisible_power_pins )
    {
        SCH_PIN* pin = it.second;

        if( !pin->ConnectedItems().empty() && !pin->GetLibPin()->GetParent()->IsPower() )
        {
            // ERC will warn about this: user has wired up an invisible pin
            continue;
        }

        SCH_SHEET_PATH sheet = it.first;
        SCH_CONNECTION* connection = pin->Connection( sheet );

        if( !connection )
            connection = pin->InitializeConnection( sheet );

        // If this pin already has a subgraph, don't need to process
        if( connection->SubgraphCode() > 0 )
            continue;

        connection->SetName( pin->GetName() );

        int code = assignNewNetCode( *connection );

        connection->SetNetCode( code );

        CONNECTION_SUBGRAPH* subgraph;

        if( invisible_pin_subgraphs.count( code ) )
        {
            subgraph = invisible_pin_subgraphs.at( code );
            subgraph->AddItem( pin );
        }
        else
        {
            subgraph = new CONNECTION_SUBGRAPH( m_frame );

            subgraph->m_code = m_last_subgraph_code++;
            subgraph->m_sheet = sheet;

            subgraph->AddItem( pin );
            subgraph->ResolveDrivers();

            m_net_code_to_subgraphs_map[ code ].push_back( subgraph );
            m_subgraphs.push_back( subgraph );
            driver_subgraphs.push_back( subgraph );

            invisible_pin_subgraphs[code] = subgraph;
        }

        connection->SetSubgraphCode( subgraph->m_code );
    }

    for( auto it : invisible_pin_subgraphs )
        it.second->UpdateItemConnections();

    // Here we do all the local (sheet) processing of each subgraph, including assigning net
    // codes, merging subgraphs together that use label connections, etc.

    std::unordered_set<CONNECTION_SUBGRAPH*> invalidated_subgraphs;

    for( auto subgraph_it = driver_subgraphs.begin();
         subgraph_it != driver_subgraphs.end(); subgraph_it++ )
    {
        auto subgraph = *subgraph_it;

        if( subgraph->m_absorbed )
            continue;

        SCH_CONNECTION* connection = subgraph->m_driver_connection;
        SCH_SHEET_PATH sheet = subgraph->m_sheet;
        wxString name = subgraph->GetNetName();
        int code = -1;

        if( connection->IsBus() )
        {
            if( m_bus_name_to_code_map.count( name ) )
            {
                code = m_bus_name_to_code_map.at( name );
            }
            else
            {
                code = m_last_bus_code++;
                m_bus_name_to_code_map[ name ] = code;
            }

            connection->SetBusCode( code );
            assignNetCodesToBus( connection );
        }
        else
        {
            assignNewNetCode( *connection );
        }

        subgraph->UpdateItemConnections();

        // Reset the flag for the next loop below
        subgraph->m_dirty = true;

        // Next, we merge together subgraphs that have label connections, and create
        // neighbor links for subgraphs that are part of a bus on the same sheet.
        // For merging, we consider each possible strong driver.

        // If this subgraph doesn't have a strong driver, let's skip it, since there is no
        // way it will be merged with anything.

        if( !subgraph->m_strong_driver )
            continue;

        // candidate_subgraphs will contain each valid, non-bus subgraph on the same sheet
        // as the subgraph we are considering that has a strong driver.
        // Weakly driven subgraphs are not considered since they will never be absorbed or
        // form neighbor links.

        std::vector<CONNECTION_SUBGRAPH*> candidate_subgraphs;
        std::copy_if( driver_subgraphs.begin(), driver_subgraphs.end(),
                      std::back_inserter( candidate_subgraphs ),
                      [&] ( const CONNECTION_SUBGRAPH* candidate )
                      { return ( !candidate->m_absorbed &&
                                 candidate->m_strong_driver &&
                                 candidate != subgraph &&
                                 candidate->m_sheet == sheet );
                      } );

        // This is a list of connections on the current subgraph to compare to the
        // drivers of each candidate subgraph.  If the current subgraph is a bus,
        // we should consider each bus member.
        auto connections_to_check( connection->Members() );

        // Also check the main driving connection
        connections_to_check.push_back( std::make_shared<SCH_CONNECTION>( *connection ) );

        auto add_connections_to_check = [&] ( CONNECTION_SUBGRAPH* aSubgraph ) {
            for( SCH_ITEM* possible_driver : aSubgraph->m_items )
            {
                if( possible_driver == aSubgraph->m_driver )
                    continue;

                switch( possible_driver->Type() )
                {
                    case SCH_PIN_T:
                    {
                        auto sch_pin = static_cast<SCH_PIN*>( possible_driver );

                        if( sch_pin->IsPowerConnection() )
                        {
                            auto c = std::make_shared<SCH_CONNECTION>( possible_driver,
                                                                       aSubgraph->m_sheet );
                            c->SetName( static_cast<SCH_PIN *>( possible_driver )->GetName() );
                            connections_to_check.push_back( c );
                            wxLogTrace( "CONN", "Adding secondary pin %s", c->Name( true ) );
                        }
                        break;
                    }

                    case SCH_GLOBAL_LABEL_T:
                    case SCH_HIER_LABEL_T:
                    case SCH_LABEL_T:
                    {
                        auto c = std::make_shared<SCH_CONNECTION>( possible_driver,
                                                                   aSubgraph->m_sheet );
                        c->SetName( static_cast<SCH_TEXT*>( possible_driver )->GetShownText() );
                        connections_to_check.push_back( c );
                        wxLogTrace( "CONN", "Adding secondary label %s", c->Name( true ) );
                        break;
                    }

                    default:
                        break;
                }
            }
        };

        // Now add other strong drivers
        // The actual connection attached to these items will have been overwritten
        // by the chosen driver of the subgraph, so we need to create a dummy connection
        add_connections_to_check( subgraph );

        for( unsigned i = 0; i < connections_to_check.size(); i++ )
        {
            auto member = connections_to_check[i];

            if( member->IsBus() )
            {
                connections_to_check.insert( connections_to_check.end(),
                                             member->Members().begin(),
                                             member->Members().end() );
            }

            wxString test_name = member->Name( true );

            for( auto candidate : candidate_subgraphs )
            {
                if( candidate->m_absorbed )
                    continue;

                bool match = false;

                if( candidate->m_driver_connection->Name( true ) == test_name )
                {
                    match = true;
                }
                else
                {
                    if( !candidate->m_multiple_drivers )
                        continue;

                    for( SCH_ITEM *driver : candidate->m_drivers )
                    {
                        if( driver == candidate->m_driver )
                            continue;

                        if( driver->Type() == SCH_PIN_T )
                        {
                            if( static_cast<SCH_PIN*>( driver )->GetName() == test_name )
                            {
                                match = true;
                                break;
                            }
                        }
                        else
                        {
                            wxASSERT( driver->Type() == SCH_LABEL_T ||
                                      driver->Type() == SCH_GLOBAL_LABEL_T ||
                                      driver->Type() == SCH_HIER_LABEL_T ||
                                      driver->Type() == SCH_SHEET_PIN_T );

                            auto text = static_cast<SCH_TEXT*>( driver );

                            if( text->GetShownText() == test_name )
                            {
                                match = true;
                                break;
                            }
                        }
                    }
                }

                if( match )
                {
                    if( connection->IsBus() && candidate->m_driver_connection->IsNet() )
                    {
                         wxLogTrace( "CONN", "%lu (%s) has neighbor %lu (%s)", subgraph->m_code,
                                     connection->Name(), candidate->m_code, member->Name() );

                        subgraph->m_bus_neighbors[ member ].push_back( candidate );
                    }
                    else
                    {
                        wxLogTrace( "CONN", "%lu (%s) absorbs neighbor %lu (%s)",
                                    subgraph->m_code, connection->Name(),
                                    candidate->m_code, member->Name() );

                        // Candidate may have other non-chosen drivers we need to follow
                        add_connections_to_check( candidate );

                        subgraph->Absorb( candidate );
                        invalidated_subgraphs.insert( subgraph );
                    }
                }
            }
        }
    }

    // Update any subgraph that was invalidated above
    for( auto subgraph : invalidated_subgraphs )
    {
        if( subgraph->m_absorbed )
            continue;

        subgraph->ResolveDrivers();

        if( subgraph->m_driver_connection->IsBus() )
            assignNetCodesToBus( subgraph->m_driver_connection );
        else
            assignNewNetCode( *subgraph->m_driver_connection );

        subgraph->UpdateItemConnections();

        wxLogTrace( "CONN", "Re-resolving drivers for %lu (%s)", subgraph->m_code,
                    subgraph->m_driver_connection->Name() );
    }

    // Absorbed subgraphs should no longer be considered
    driver_subgraphs.erase( std::remove_if( driver_subgraphs.begin(), driver_subgraphs.end(),
                            [&] ( const CONNECTION_SUBGRAPH* candidate ) -> bool {
                                return candidate->m_absorbed;
                            } ), driver_subgraphs.end() );

    // Store global subgraphs for later reference
    std::vector<CONNECTION_SUBGRAPH*> global_subgraphs;
    std::copy_if( driver_subgraphs.begin(), driver_subgraphs.end(),
                  std::back_inserter( global_subgraphs ),
                  [&] ( const CONNECTION_SUBGRAPH* candidate ) -> bool {
                      return !candidate->m_local_driver;
                  } );

    // Next time through the subgraphs, we do some post-processing to handle things like
    // connecting bus members to their neighboring subgraphs, and then propagate connections
    // through the hierarchy

    for( auto subgraph : driver_subgraphs )
    {
        if( !subgraph->m_dirty )
            continue;

        // For subgraphs that are driven by a global (power port or label) and have more
        // than one global driver, we need to seek out other subgraphs driven by the
        // same name as the non-chosen driver and update them to match the chosen one.

        if( !subgraph->m_local_driver && subgraph->m_multiple_drivers )
        {
            for( SCH_ITEM* driver : subgraph->m_drivers )
            {
                if( driver == subgraph->m_driver )
                    continue;

                wxString secondary_name = subgraph->GetNameForDriver( driver );

                if( secondary_name == subgraph->m_driver_connection->Name() )
                    continue;

                for( CONNECTION_SUBGRAPH* candidate : global_subgraphs )
                {
                    if( candidate == subgraph )
                        continue;

                    SCH_CONNECTION* conn = candidate->m_driver_connection;

                    if( conn->Name() == secondary_name )
                    {
                        wxLogTrace( "CONN", "Global %lu (%s) promoted to %s", candidate->m_code,
                                    conn->Name(), subgraph->m_driver_connection->Name() );

                        conn->Clone( *subgraph->m_driver_connection );
                        candidate->UpdateItemConnections();
                    }
                }
            }
        }

        // This call will handle descending the hierarchy and updating child subgraphs
        propagateToNeighbors( subgraph );

        subgraph->m_dirty = false;
    }

    m_net_code_to_subgraphs_map.clear();

    for( auto subgraph : driver_subgraphs )
    {
        if( subgraph->m_dirty )
            subgraph->m_dirty = false;

        if( subgraph->m_driver_connection->IsBus() )
            continue;

        int code = subgraph->m_driver_connection->NetCode();
        m_net_code_to_subgraphs_map[ code ].push_back( subgraph );
    }

    m_subgraphs.erase( std::remove_if( m_subgraphs.begin(), m_subgraphs.end(),
                                 [&] ( const CONNECTION_SUBGRAPH* sg ) {
                                         return sg->m_absorbed;
                                     } ), m_subgraphs.end() );

    phase2.Stop();
    wxLogTrace( "CONN_PROFILE", "BuildConnectionGraph() %0.4f ms", phase2.msecs() );
}


int CONNECTION_GRAPH::assignNewNetCode( SCH_CONNECTION& aConnection )
{
    int code;

    if( m_net_name_to_code_map.count( aConnection.Name() ) )
    {
        code = m_net_name_to_code_map.at( aConnection.Name() );
    }
    else
    {
        code = m_last_net_code++;
        m_net_name_to_code_map[ aConnection.Name() ] = code;
    }

    aConnection.SetNetCode( code );

    return code;
}


void CONNECTION_GRAPH::assignNetCodesToBus( SCH_CONNECTION* aConnection )
{
    auto connections_to_check( aConnection->Members() );

    for( unsigned i = 0; i < connections_to_check.size(); i++ )
    {
        auto member = connections_to_check[i];

        if( member->IsBus() )
        {
            connections_to_check.insert( connections_to_check.end(),
                                         member->Members().begin(),
                                         member->Members().end() );
            continue;
        }

        assignNewNetCode( *member );
    }
}


void CONNECTION_GRAPH::propagateToNeighbors( CONNECTION_SUBGRAPH* aSubgraph )
{
    SCH_CONNECTION* conn = aSubgraph->m_driver_connection;
    std::vector<CONNECTION_SUBGRAPH*> children;

    auto add_children = [&] ( CONNECTION_SUBGRAPH* aParent ) {
        for( SCH_SHEET_PIN* sheet_pin : aParent->m_hier_pins )
        {
            wxString pin_name = sheet_pin->GetShownText();
            SCH_SHEET_PATH path = aParent->m_sheet;
            path.push_back( sheet_pin->GetParent() );

            // TODO(JE) is it worth changing this to driver_subgraphs from buildConnectionGraph?
            for( auto candidate : m_subgraphs )
            {
                if( candidate->m_absorbed ||
                    !candidate->m_driver ||
                    candidate->m_hier_ports.empty() ||
                    candidate->m_sheet != path )
                    continue;

                for( SCH_HIERLABEL* label : candidate->m_hier_ports )
                {
                    if( label->GetShownText() == pin_name )
                    {
                        wxLogTrace( "CONN", "Found child %lu (%s)",
                                    candidate->m_code, candidate->m_driver_connection->Name() );

                        children.push_back( candidate );
                        break;
                    }
                }
            }
        }
    };

    auto propagate_bus_neighbors = [&] ( CONNECTION_SUBGRAPH* aParent ) {
        for( const auto& kv : aParent->m_bus_neighbors )
        {
            for( CONNECTION_SUBGRAPH* neighbor : kv.second )
            {
                // May have been absorbed but won't have been deleted
                if( neighbor->m_absorbed )
                    continue;

                SCH_CONNECTION* parent = aParent->m_driver_connection;
                SCH_CONNECTION* member = nullptr;

                // Now member may be out of date, since we just cloned the
                // connection from higher up in the hierarchy.  We need to
                // figure out what the actual new connection is.

                if( parent->Type() == CONNECTION_BUS )
                {
                    // Vector bus: compare against index, because we allow the name
                    // to be different

                    for( const auto &bus_member : parent->Members() )
                    {
                        if( bus_member->VectorIndex() == kv.first->VectorIndex() )
                        {
                            member = bus_member.get();
                            break;
                        }
                    }
                }
                else
                {
                    // Group bus
                    for( const auto &c : parent->Members() )
                    {
                        // Vector inside group: compare names, because for bus groups
                        // we expect the naming to be consistent across all usages
                        // TODO(JE) explain this in the docs
                        if( c->Type() == CONNECTION_BUS )
                        {
                            for( const auto &bus_member : c->Members() )
                            {
                                if( bus_member->RawName() == kv.first->RawName() )
                                {
                                    member = bus_member.get();
                                    break;
                                }
                            }
                        }
                        else if( c->RawName() == kv.first->RawName() )
                        {
                            member = c.get();
                            break;
                        }
                    }
                }

                // This is bad, probably an ERC error
                if( !member )
                {
                    wxLogTrace( "CONN", "Could not match bus member %s in %s",
                                kv.first->Name(), parent->Name() );
                    continue;
                }

                auto neighbor_conn = neighbor->m_driver_connection;

                // TODO(JE) check if this is too slow
                if( neighbor_conn->Name() == member->Name() )
                    continue;

                wxLogTrace( "CONN", "%lu (%s) connected to bus member %s",
                            neighbor->m_code, neighbor_conn->Name(), member->Name() );

                neighbor_conn->Clone( *member );
                neighbor->UpdateItemConnections();
            }
        }
    };

    // If this is a plain net, all neighbors on the same sheet will already have been
    // absorbed into this one.  So, the only thing to do is check the hierarchy.

    if( conn->IsNet() )
    {
        if( aSubgraph->m_hier_pins.empty() )
            return;

        wxLogTrace( "CONN", "Propagating %lu (%s) to subsheets",
                    aSubgraph->m_code, aSubgraph->m_driver_connection->Name() );

        add_children( aSubgraph );

        for( unsigned i = 0; i < children.size(); i++ )
        {
            auto child = children[i];

            // Check for grandchildren
            if( !child->m_hier_pins.empty() )
                add_children( child );

            child->m_driver_connection->Clone( *conn );
            child->UpdateItemConnections();
        }

        return;
    }

    // Otherwise, we are a bus, so we must propagate to local neighbors and then the hierarchy
    propagate_bus_neighbors( aSubgraph );

    if( aSubgraph->m_hier_pins.empty() )
        return;

    // TODO(JE) this code looks very similar to the Net loop above, can it be merged?

    wxLogTrace( "CONN", "Propagating %lu (%s) to subsheets",
                aSubgraph->m_code, aSubgraph->m_driver_connection->Name() );

    add_children( aSubgraph );

    for( unsigned i = 0; i < children.size(); i++ )
    {
        auto child = children[i];

        // Check for grandchildren
        if( !child->m_hier_pins.empty() )
            add_children( child );

        child->m_driver_connection->Clone( *conn );
        child->UpdateItemConnections();

        propagate_bus_neighbors( child );
    }
}


std::shared_ptr<BUS_ALIAS> CONNECTION_GRAPH::GetBusAlias( wxString aName )
{
    if( m_bus_alias_cache.count( aName ) )
        return m_bus_alias_cache.at( aName );

    return nullptr;
}


std::vector<const CONNECTION_SUBGRAPH*> CONNECTION_GRAPH::GetBusesNeedingMigration()
{
    std::vector<const CONNECTION_SUBGRAPH*> ret;

    for( auto&& subgraph : m_subgraphs )
    {
        // Graph is supposed to be up-to-date before calling this
        wxASSERT( !subgraph->m_dirty );

        if( !subgraph->m_driver )
            continue;

        auto sheet = subgraph->m_sheet;
        auto connection = subgraph->m_driver->Connection( sheet );

        if( !connection->IsBus() )
            continue;

        auto labels = subgraph->GetBusLabels();

        if( labels.size() > 1 )
        {
            bool different = false;
            wxString first = static_cast<SCH_TEXT*>( labels.at( 0 ) )->GetText();

            for( unsigned i = 1; i < labels.size(); ++i )
            {
                if( static_cast<SCH_TEXT*>( labels.at( i ) )->GetText() != first )
                {
                    different = true;
                    break;
                }
            }

            if( !different )
                continue;

            wxLogTrace( "CONN", "SG %ld (%s) has multiple bus labels", subgraph->m_code,
                        connection->Name() );

            ret.push_back( subgraph );
        }
    }

    return ret;
}


bool CONNECTION_GRAPH::UsesNewBusFeatures() const
{
    for( auto&& subgraph : m_subgraphs )
    {
        if( !subgraph->m_driver )
            continue;

        auto sheet = subgraph->m_sheet;
        auto connection = subgraph->m_driver->Connection( sheet );

        if( !connection->IsBus() )
            continue;

        if( connection->Type() == CONNECTION_BUS_GROUP )
            return true;
    }

    return false;
}


int CONNECTION_GRAPH::RunERC( const ERC_SETTINGS& aSettings, bool aCreateMarkers )
{
    int error_count = 0;

    std::map< wxString, std::vector< std::pair< SCH_ITEM*, const CONNECTION_SUBGRAPH* > > > globals;

    for( auto&& subgraph : m_subgraphs )
    {
        // Graph is supposed to be up-to-date before calling RunERC()
        wxASSERT( !subgraph->m_dirty );

        for( const auto& item : subgraph->m_items )
        {
            if( item->Type() == SCH_GLOBAL_LABEL_T )
            {
                wxString key = static_cast<SCH_TEXT*>( item )->GetText();
                globals[ key ].emplace_back( std::make_pair( item, subgraph ) );
            }
        }

        /**
         * NOTE:
         *
         * We could check that labels attached to bus subgraphs follow the
         * proper format (i.e. actually define a bus).
         *
         * This check doesn't need to be here right now because labels
         * won't actually be connected to bus wires if they aren't in the right
         * format due to their TestDanglingEnds() implementation.
         */

        if( aSettings.check_bus_driver_conflicts &&
            !subgraph->ResolveDrivers( aCreateMarkers ) )
            error_count++;

        if( aSettings.check_bus_to_net_conflicts &&
            !ercCheckBusToNetConflicts( subgraph, aCreateMarkers ) )
            error_count++;

        if( aSettings.check_bus_entry_conflicts &&
            !ercCheckBusToBusEntryConflicts( subgraph, aCreateMarkers ) )
            error_count++;

        if( aSettings.check_bus_to_bus_conflicts &&
            !ercCheckBusToBusConflicts( subgraph, aCreateMarkers ) )
            error_count++;

        // The following checks are always performed since they don't currently
        // have an option exposed to the user

        if( !ercCheckNoConnects( subgraph, aCreateMarkers ) )
            error_count++;

        if( !ercCheckLabels( subgraph, aCreateMarkers ) )
            error_count++;
    }

    // Some checks are now run after processing every subgraph

    // Check for lonely global labels
    if( aSettings.check_unique_global_labels )
    {
        for( auto &it : globals )
        {
            if( it.second.size() == 1 )
            {
                ercReportIsolatedGlobalLabel( it.second.at( 0 ).second, it.second.at( 0 ).first );
                error_count++;
            }
        }
    }

    return error_count;
}


void CONNECTION_GRAPH::ercReportIsolatedGlobalLabel( const CONNECTION_SUBGRAPH* aSubgraph,
                                                     SCH_ITEM* aLabel )
{
    wxString msg;
    auto label = dynamic_cast<SCH_TEXT*>( aLabel );

    if( !label )
        return;

    msg.Printf( _( "Global label %s is not connected to any other global label." ),
                label->GetShownText() );

    auto marker = new SCH_MARKER();
    marker->SetTimeStamp( GetNewTimeStamp() );
    marker->SetMarkerType( MARKER_BASE::MARKER_ERC );
    marker->SetErrorLevel( MARKER_BASE::MARKER_SEVERITY_WARNING );
    marker->SetData( ERCE_GLOBLABEL,
                     label->GetPosition(),
                     msg,
                     label->GetPosition() );

    SCH_SCREEN* screen = aSubgraph->m_sheet.LastScreen();
    screen->Append( marker );
}


bool CONNECTION_GRAPH::ercCheckBusToNetConflicts( const CONNECTION_SUBGRAPH* aSubgraph,
                                                  bool aCreateMarkers )
{
    wxString msg;
    auto sheet = aSubgraph->m_sheet;
    auto screen = sheet.LastScreen();

    SCH_ITEM* net_item = nullptr;
    SCH_ITEM* bus_item = nullptr;
    SCH_CONNECTION conn;

    for( auto item : aSubgraph->m_items )
    {
        switch( item->Type() )
        {
        case SCH_LINE_T:
        {
            if( item->GetLayer() == LAYER_BUS )
                bus_item = ( !bus_item ) ? item : bus_item;
            else
                net_item = ( !net_item ) ? item : net_item;
            break;
        }

        case SCH_GLOBAL_LABEL_T:
        case SCH_SHEET_PIN_T:
        case SCH_HIER_LABEL_T:
        {
            auto text = static_cast<SCH_TEXT*>( item )->GetShownText();
            conn.ConfigureFromLabel( text );

            if( conn.IsBus() )
                bus_item = ( !bus_item ) ? item : bus_item;
            else
                net_item = ( !net_item ) ? item : net_item;
            break;
        }

        default:
            break;
        }
    }

    if( net_item && bus_item )
    {
        if( aCreateMarkers )
        {
            msg.Printf( _( "%s and %s are graphically connected but cannot"
                           " electrically connect because one is a bus and"
                           " the other is a net." ),
                        bus_item->GetSelectMenuText( m_frame->GetUserUnits() ),
                        net_item->GetSelectMenuText( m_frame->GetUserUnits() ) );

            auto marker = new SCH_MARKER();
            marker->SetTimeStamp( GetNewTimeStamp() );
            marker->SetMarkerType( MARKER_BASE::MARKER_ERC );
            marker->SetErrorLevel( MARKER_BASE::MARKER_SEVERITY_ERROR );
            marker->SetData( ERCE_BUS_TO_NET_CONFLICT,
                             net_item->GetPosition(), msg,
                             bus_item->GetPosition() );

            screen->Append( marker );
        }

        return false;
    }

    return true;
}


bool CONNECTION_GRAPH::ercCheckBusToBusConflicts( const CONNECTION_SUBGRAPH* aSubgraph,
                                                  bool aCreateMarkers )
{
    wxString msg;
    auto sheet = aSubgraph->m_sheet;
    auto screen = sheet.LastScreen();

    SCH_ITEM* label = nullptr;
    SCH_ITEM* port = nullptr;

    for( auto item : aSubgraph->m_items )
    {
        switch( item->Type() )
        {
        case SCH_TEXT_T:
        case SCH_GLOBAL_LABEL_T:
        {
            if( !label && item->Connection( sheet )->IsBus() )
                label = item;
            break;
        }

        case SCH_SHEET_PIN_T:
        case SCH_HIER_LABEL_T:
        {
            if( !port && item->Connection( sheet )->IsBus() )
                port = item;
            break;
        }

        default:
            break;
        }
    }

    if( label && port )
    {
        bool match = false;

        for( const auto& member : label->Connection( sheet )->Members() )
        {
            for( const auto& test : port->Connection( sheet )->Members() )
            {
                if( test != member && member->Name() == test->Name() )
                {
                    match = true;
                    break;
                }
            }

            if( match )
                break;
        }

        if( !match )
        {
            if( aCreateMarkers )
            {
                msg.Printf( _( "%s and %s are graphically connected but do "
                               "not share any bus members" ),
                            label->GetSelectMenuText( m_frame->GetUserUnits() ),
                            port->GetSelectMenuText( m_frame->GetUserUnits() ) );

                auto marker = new SCH_MARKER();
                marker->SetTimeStamp( GetNewTimeStamp() );
                marker->SetMarkerType( MARKER_BASE::MARKER_ERC );
                marker->SetErrorLevel( MARKER_BASE::MARKER_SEVERITY_ERROR );
                marker->SetData( ERCE_BUS_TO_BUS_CONFLICT,
                                 label->GetPosition(), msg,
                                 port->GetPosition() );

                screen->Append( marker );
            }

            return false;
        }
    }

    return true;
}


bool CONNECTION_GRAPH::ercCheckBusToBusEntryConflicts( const CONNECTION_SUBGRAPH* aSubgraph,
                                                       bool aCreateMarkers )
{
    wxString msg;
    bool conflict = false;
    auto sheet = aSubgraph->m_sheet;
    auto screen = sheet.LastScreen();

    SCH_BUS_WIRE_ENTRY* bus_entry = nullptr;
    SCH_ITEM* bus_wire = nullptr;

    for( auto item : aSubgraph->m_items )
    {
        switch( item->Type() )
        {
        case SCH_BUS_WIRE_ENTRY_T:
        {
            if( !bus_entry )
                bus_entry = static_cast<SCH_BUS_WIRE_ENTRY*>( item );
            break;
        }

        default:
            break;
        }
    }

    if( bus_entry && bus_entry->m_connected_bus_item )
    {
        bus_wire = bus_entry->m_connected_bus_item;
        conflict = true;

        auto test_name = bus_entry->Connection( sheet )->Name();

        for( auto member : bus_wire->Connection( sheet )->Members() )
        {
            if( member->Type() == CONNECTION_BUS )
            {
                for( const auto& sub_member : member->Members() )
                    if( sub_member->Name() == test_name )
                        conflict = false;
            }
            else if( member->Name() == test_name )
            {
                conflict = false;
            }
        }
    }

    if( conflict )
    {
        if( aCreateMarkers )
        {
            msg.Printf( _( "%s (%s) is connected to %s (%s) but is not a member of the bus" ),
                        bus_entry->GetSelectMenuText( m_frame->GetUserUnits() ),
                        bus_entry->Connection( sheet )->Name(),
                        bus_wire->GetSelectMenuText( m_frame->GetUserUnits() ),
                        bus_wire->Connection( sheet )->Name() );

            auto marker = new SCH_MARKER();
            marker->SetTimeStamp( GetNewTimeStamp() );
            marker->SetMarkerType( MARKER_BASE::MARKER_ERC );
            marker->SetErrorLevel( MARKER_BASE::MARKER_SEVERITY_WARNING );
            marker->SetData( ERCE_BUS_ENTRY_CONFLICT,
                             bus_entry->GetPosition(), msg,
                             bus_entry->GetPosition() );

            screen->Append( marker );
        }

        return false;
    }

    return true;
}


// TODO(JE) Check sheet pins here too?
bool CONNECTION_GRAPH::ercCheckNoConnects( const CONNECTION_SUBGRAPH* aSubgraph,
                                           bool aCreateMarkers )
{
    wxString msg;
    auto sheet = aSubgraph->m_sheet;
    auto screen = sheet.LastScreen();

    if( aSubgraph->m_no_connect != nullptr )
    {
        bool has_invalid_items = false;
        bool has_other_items = false;
        SCH_PIN* pin = nullptr;
        std::vector<SCH_ITEM*> invalid_items;

        // Any subgraph that contains both a pin and a no-connect should not
        // contain any other driving items.

        for( auto item : aSubgraph->m_items )
        {
            switch( item->Type() )
            {
            case SCH_PIN_T:
                pin = static_cast<SCH_PIN*>( item );
                has_other_items = true;
                break;

            case SCH_LINE_T:
            case SCH_JUNCTION_T:
            case SCH_NO_CONNECT_T:
                break;

            default:
                has_invalid_items = true;
                has_other_items = true;
                invalid_items.push_back( item );
            }
        }

        if( pin && has_invalid_items )
        {
            wxPoint pos = pin->GetTransformedPosition();

            msg.Printf( _( "Pin %s of component %s has a no-connect marker but is connected" ),
                        GetChars( pin->GetName() ),
                        GetChars( pin->GetParentComponent()->GetRef( &aSubgraph->m_sheet ) ) );

            auto marker = new SCH_MARKER();
            marker->SetTimeStamp( GetNewTimeStamp() );
            marker->SetMarkerType( MARKER_BASE::MARKER_ERC );
            marker->SetErrorLevel( MARKER_BASE::MARKER_SEVERITY_WARNING );
            marker->SetData( ERCE_NOCONNECT_CONNECTED, pos, msg, pos );

            screen->Append( marker );

            return false;
        }

        if( !has_other_items )
        {
            wxPoint pos = aSubgraph->m_no_connect->GetPosition();

            msg.Printf( _( "No-connect marker is not connected to anything" ) );

            auto marker = new SCH_MARKER();
            marker->SetTimeStamp( GetNewTimeStamp() );
            marker->SetMarkerType( MARKER_BASE::MARKER_ERC );
            marker->SetErrorLevel( MARKER_BASE::MARKER_SEVERITY_WARNING );
            marker->SetData( ERCE_NOCONNECT_NOT_CONNECTED, pos, msg, pos );

            screen->Append( marker );

            return false;
        }
    }
    else
    {
        bool has_other_connections = false;
        SCH_PIN* pin = nullptr;

        // Any subgraph that lacks a no-connect and contains a pin should also
        // contain at least one other connectable item.

        for( auto item : aSubgraph->m_items )
        {
            switch( item->Type() )
            {
            case SCH_PIN_T:
                if( !pin )
                    pin = static_cast<SCH_PIN*>( item );
                else
                    has_other_connections = true;
                break;

            default:
                if( item->IsConnectable() )
                    has_other_connections = true;
                break;
            }
        }

        // Check if invisible power pins connect to anything else
        // Note this won't catch if a component has multiple invisible power
        // pins but these don't connect to any other net; maybe that should be
        // added as a further optional ERC check.

        if( pin && !has_other_connections &&
            pin->IsPowerConnection() && !pin->IsVisible() )
        {
            wxString name = pin->Connection( sheet )->Name();
            wxString local_name = pin->Connection( sheet )->Name( true );

            if( m_global_label_cache.count( name )  ||
                ( m_local_label_cache.count( std::make_pair( sheet, local_name ) ) ) )
            {
                has_other_connections = true;
            }
        }

        if( pin && !has_other_connections && pin->GetType() != PIN_NC )
        {
            wxPoint pos = pin->GetTransformedPosition();

            msg.Printf( _( "Pin %s of component %s is unconnected." ),
                        GetChars( pin->GetName() ),
                        GetChars( pin->GetParentComponent()->GetRef( &aSubgraph->m_sheet ) ) );

            auto marker = new SCH_MARKER();
            marker->SetTimeStamp( GetNewTimeStamp() );
            marker->SetMarkerType( MARKER_BASE::MARKER_ERC );
            marker->SetErrorLevel( MARKER_BASE::MARKER_SEVERITY_WARNING );
            marker->SetData( ERCE_PIN_NOT_CONNECTED, pos, msg, pos );

            screen->Append( marker );

            return false;
        }
    }

    return true;
}


bool CONNECTION_GRAPH::ercCheckLabels( const CONNECTION_SUBGRAPH* aSubgraph,
                                       bool aCreateMarkers )
{
    wxString msg;
    auto sheet = aSubgraph->m_sheet;
    auto screen = sheet.LastScreen();

    SCH_TEXT* text = nullptr;
    bool has_other_connections = false;

    // Any subgraph that contains a label should also contain at least one other
    // connectable item.

    for( auto item : aSubgraph->m_items )
    {
        switch( item->Type() )
        {
        case SCH_LABEL_T:
        case SCH_GLOBAL_LABEL_T:
        case SCH_HIER_LABEL_T:
            text = static_cast<SCH_TEXT*>( item );
            break;

        case SCH_PIN_T:
            has_other_connections = true;
            break;

        default:
            if( item->IsConnectable() )
                has_other_connections = true;
            break;
        }
    }

    if( text && !has_other_connections )
    {
        auto pos = text->GetPosition();
        msg.Printf( _( "Label %s is unconnected." ),
                    GetChars( text->ShortenedShownText() ) );

        auto marker = new SCH_MARKER();
        marker->SetTimeStamp( GetNewTimeStamp() );
        marker->SetMarkerType( MARKER_BASE::MARKER_ERC );
        marker->SetErrorLevel( MARKER_BASE::MARKER_SEVERITY_WARNING );
        marker->SetData( ERCE_LABEL_NOT_CONNECTED, pos, msg, pos );

        screen->Append( marker );

        return false;
    }

    return true;
}
