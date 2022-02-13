
/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2016-2018 CERN
 * Copyright (C) 2019-2022 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * @author Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
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

#include <core/kicad_algo.h>
#include <macros.h>
#include <connectivity/connectivity_items.h>
#include <trigo.h>

#include <wx/log.h>

int CN_ITEM::AnchorCount() const
{
    if( !m_valid )
        return 0;

    switch( m_parent->Type() )
    {
    case PCB_TRACE_T:
    case PCB_ARC_T:
        return 2;  // start and end
    default:
        return 1;
    }
}


const VECTOR2I CN_ITEM::GetAnchor( int n ) const
{
    if( !m_valid )
        return VECTOR2I();

    switch( m_parent->Type() )
    {
    case PCB_PAD_T:
        return static_cast<PAD*>( m_parent )->GetPosition();

    case PCB_TRACE_T:
    case PCB_ARC_T:
        if( n == 0 )
            return static_cast<const PCB_TRACK*>( m_parent )->GetStart();
        else
            return static_cast<const PCB_TRACK*>( m_parent )->GetEnd();

    case PCB_VIA_T:
        return static_cast<const PCB_VIA*>( m_parent )->GetStart();

    default:
        UNIMPLEMENTED_FOR( m_parent->GetClass() );
        return VECTOR2I();
    }
}


void CN_ITEM::Dump()
{
    wxLogDebug("    valid: %d, connected: \n", !!Valid());

    for( CN_ITEM* i : m_connected )
    {
        PCB_TRACK* t = static_cast<PCB_TRACK*>( i->Parent() );
        wxLogDebug( wxT( "    - %p %d\n" ), t, t->Type() );
    }
}


int CN_ZONE_LAYER::AnchorCount() const
{
    if( !Valid() )
        return 0;

    const ZONE*             zone    = static_cast<const ZONE*>( Parent() );
    const SHAPE_LINE_CHAIN& outline = zone->GetFilledPolysList( m_layer ).COutline( m_subpolyIndex );

    return outline.PointCount() ? 1 : 0;
}


const VECTOR2I CN_ZONE_LAYER::GetAnchor( int n ) const
{
    if( !Valid() )
        return VECTOR2I();

    const ZONE*             zone    = static_cast<const ZONE*>( Parent() );
    const SHAPE_LINE_CHAIN& outline = zone->GetFilledPolysList( m_layer ).COutline( m_subpolyIndex );

    return outline.CPoint( 0 );
}


void CN_ITEM::RemoveInvalidRefs()
{
    for( auto it = m_connected.begin(); it != m_connected.end(); )
    {
        if( !(*it)->Valid() )
            it = m_connected.erase( it );
        else
            ++it;
    }
}


CN_ITEM* CN_LIST::Add( PAD* pad )
 {
    if( !pad->IsOnCopperLayer() )
         return nullptr;

     auto item = new CN_ITEM( pad, false, 1 );
     item->AddAnchor( pad->ShapePos() );
     item->SetLayers( LAYER_RANGE( F_Cu, B_Cu ) );

     switch( pad->GetAttribute() )
     {
     case PAD_ATTRIB::SMD:
     case PAD_ATTRIB::NPTH:
     case PAD_ATTRIB::CONN:
     {
         LSET lmsk = pad->GetLayerSet();

         for( int i = 0; i <= MAX_CU_LAYERS; i++ )
         {
             if( lmsk[i] )
             {
                 item->SetLayer( i );
                 break;
             }
         }
         break;
     }
     default:
         break;
     }

     addItemtoTree( item );
     m_items.push_back( item );
     SetDirty();
     return item;
}

CN_ITEM* CN_LIST::Add( PCB_TRACK* track )
{
    auto item = new CN_ITEM( track, true );
    m_items.push_back( item );
    item->AddAnchor( track->GetStart() );
    item->AddAnchor( track->GetEnd() );
    item->SetLayer( track->GetLayer() );
    addItemtoTree( item );
    SetDirty();
    return item;
}

CN_ITEM* CN_LIST::Add( PCB_ARC* aArc )
{
    auto item = new CN_ITEM( aArc, true );
    m_items.push_back( item );
    item->AddAnchor( aArc->GetStart() );
    item->AddAnchor( aArc->GetEnd() );
    item->SetLayer( aArc->GetLayer() );
    addItemtoTree( item );
    SetDirty();
    return item;
}

 CN_ITEM* CN_LIST::Add( PCB_VIA* via )
 {
     auto item = new CN_ITEM( via, !via->GetIsFree(), 1 );

     m_items.push_back( item );
     item->AddAnchor( via->GetStart() );

     item->SetLayers( LAYER_RANGE( via->TopLayer(), via->BottomLayer() ) );
     addItemtoTree( item );
     SetDirty();
     return item;
 }

 const std::vector<CN_ITEM*> CN_LIST::Add( ZONE* zone, PCB_LAYER_ID aLayer )
 {
     const auto& polys = zone->GetFilledPolysList( aLayer );

     std::vector<CN_ITEM*> rv;

     for( int j = 0; j < polys.OutlineCount(); j++ )
     {
         CN_ZONE_LAYER* zitem = new CN_ZONE_LAYER( zone, aLayer, false, j );
         const auto& outline = zone->GetFilledPolysList( aLayer ).COutline( j );

         for( int k = 0; k < outline.PointCount(); k++ )
             zitem->AddAnchor( outline.CPoint( k ) );

         m_items.push_back( zitem );
         zitem->SetLayer( aLayer );
         addItemtoTree( zitem );
         rv.push_back( zitem );
         SetDirty();
     }

     return rv;
 }


void CN_LIST::RemoveInvalidItems( std::vector<CN_ITEM*>& aGarbage )
{
    if( !m_hasInvalid )
        return;

    auto lastItem = std::remove_if(m_items.begin(), m_items.end(), [&aGarbage] ( CN_ITEM* item )
    {
        if( !item->Valid() )
        {
            aGarbage.push_back ( item );
            return true;
        }

        return false;
    } );

    m_items.resize( lastItem - m_items.begin() );

    for( auto item : m_items )
        item->RemoveInvalidRefs();

    for( auto item : aGarbage )
        m_index.Remove( item );

    m_hasInvalid = false;
}


BOARD_CONNECTED_ITEM* CN_ANCHOR::Parent() const
{
    assert( m_item->Valid() );
    return m_item->Parent();
}


bool CN_ANCHOR::Valid() const
{
    if( !m_item )
        return false;

    return m_item->Valid();
}


bool CN_ANCHOR::IsDangling() const
{
    int accuracy = 0;

    if( !m_cluster )
        return true;

    // the minimal number of items connected to item_ref
    // at this anchor point to decide the anchor is *not* dangling
    size_t minimal_count = 1;
    size_t connected_count = m_item->ConnectedItems().size();

    // a via can be removed if connected to only one other item.
    if( Parent()->Type() == PCB_VIA_T )
        return connected_count < 2;

    if( m_item->AnchorCount() == 1 )
        return connected_count < minimal_count;

    if( Parent()->Type() == PCB_TRACE_T || Parent()->Type() == PCB_ARC_T )
        accuracy = KiROUND( static_cast<const PCB_TRACK*>( Parent() )->GetWidth() / 2 );

    // Items with multiple anchors have usually items connected to each anchor.
    // We want only the item count of this anchor point
    connected_count = 0;

    for( CN_ITEM* item : m_item->ConnectedItems() )
    {
        if( item->Parent()->Type() == PCB_ZONE_T )
        {
            ZONE* zone = static_cast<ZONE*>( item->Parent() );

            if( zone->HitTestFilledArea( ToLAYER_ID( item->Layer() ), Pos(), accuracy ) )
                connected_count++;
        }
        else if( item->Parent()->HitTest( Pos(), accuracy ) )
        {
            connected_count++;
        }
    }

    return connected_count < minimal_count;
}


int CN_ANCHOR::ConnectedItemsCount() const
{
    if( !m_cluster )
        return 0;

    int connected_count = 0;

    for( CN_ITEM* item : m_item->ConnectedItems() )
    {
        if( item->Parent()->Type() == PCB_ZONE_T )
        {
            ZONE* zone = static_cast<ZONE*>( item->Parent() );

            if( zone->HitTestFilledArea( ToLAYER_ID( item->Layer() ), Pos() ) )
                connected_count++;
        }
        else if( item->Parent()->HitTest( Pos() ) )
        {
            connected_count++;
        }
    }

    return connected_count;
}


CN_CLUSTER::CN_CLUSTER()
{
    m_items.reserve( 64 );
    m_originPad = nullptr;
    m_originNet = -1;
    m_conflicting = false;
}


CN_CLUSTER::~CN_CLUSTER()
{
}


wxString CN_CLUSTER::OriginNetName() const
{
    if( !m_originPad || !m_originPad->Valid() )
        return "<none>";
    else
        return m_originPad->Parent()->GetNetname();
}


bool CN_CLUSTER::Contains( const CN_ITEM* aItem )
{
    return alg::contains( m_items, aItem );
}


bool CN_CLUSTER::Contains( const BOARD_CONNECTED_ITEM* aItem )
{
    return std::find_if( m_items.begin(), m_items.end(),
                         [ &aItem ] ( const CN_ITEM* item )
                         {
                             return item->Valid() && item->Parent() == aItem;
                         } ) != m_items.end();
}


void CN_CLUSTER::Dump()
{
    for( auto item : m_items )
    {
        wxLogTrace( wxT( "CN" ), wxT( " - item : %p bitem : %p type : %d inet %s\n" ),
                    item,
                    item->Parent(),
                    item->Parent()->Type(),
                    (const char*) item->Parent()->GetNetname().c_str() );
        wxLogTrace( wxT( "CN" ), wxT( "- item : %p bitem : %p type : %d inet %s\n" ),
                    item,
                    item->Parent(),
                    item->Parent()->Type(),
                    (const char*) item->Parent()->GetNetname().c_str() );
        item->Dump();
    }
}


void CN_CLUSTER::Add( CN_ITEM* item )
{
    m_items.push_back( item );

    int netCode = item->Net();

    if( netCode <= 0 )
        return;

    if( m_originNet <= 0 )
    {
        m_originNet = netCode;
    }

    if( item->Parent()->Type() == PCB_PAD_T )
    {
        if( m_netRanks.count( netCode ) )
        {
            m_netRanks[netCode]++;

            if( m_netRanks.count( m_originNet ) && m_netRanks[netCode] > m_netRanks[m_originNet] )
            {
                m_originPad = item;
                m_originNet = netCode;
            }
        }
        else
        {
            m_netRanks[netCode] = 1;

            if( !m_originPad )
            {
                m_originPad = item;
                m_originNet = netCode;
            }
        }

        if( m_originPad && item->Net() != m_originNet )
        {
            m_conflicting = true;
        }
    }
}
