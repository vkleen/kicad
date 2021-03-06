/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2022 KiCad Developers, see AUTHORS.txt for contributors.
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

#include <drc/drc_engine.h>
#include <drc/drc_item.h>
#include <drc/drc_test_provider.h>
#include <footprint.h>
#include <pad.h>

/*
    Footprint tests:

    - DRCE_FOOTPRINT_TYPE_MISMATCH,
    - DRCE_OVERLAPPING_PADS,
    - DRCE_PAD_TH_WITH_NO_HOLE,
    - DRCE_PADSTACK
*/

class DRC_TEST_PROVIDER_FOOTPRINT_CHECKS : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_FOOTPRINT_CHECKS()
    {
        m_isRuleDriven = false;
    }

    virtual ~DRC_TEST_PROVIDER_FOOTPRINT_CHECKS()
    {
    }

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return wxT( "footprint checks" );
    };

    virtual const wxString GetDescription() const override
    {
        return wxT( "Check for common footprint pad and component type errors" );
    }
};


bool DRC_TEST_PROVIDER_FOOTPRINT_CHECKS::Run()
{
    if( !reportPhase( _( "Checking footprints..." ) ) )
        return false;   // DRC cancelled

    auto errorHandler =
            [&]( const BOARD_ITEM* aItemA, const BOARD_ITEM* aItemB, int aErrorCode,
                 const wxString& aMsg, const VECTOR2I& aPt, PCB_LAYER_ID aLayer )
            {
                std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( aErrorCode );

                if( !aMsg.IsEmpty() )
                    drcItem->SetErrorMessage( drcItem->GetErrorText() + wxS( " " ) + aMsg );

                drcItem->SetItems( aItemA, aItemB );
                reportViolation( drcItem, aPt, aLayer );
            };

    for( FOOTPRINT* footprint : m_drcEngine->GetBoard()->Footprints() )
    {
        if( !m_drcEngine->IsErrorLimitExceeded( DRCE_FOOTPRINT_TYPE_MISMATCH ) )
        {
            footprint->CheckFootprintAttributes(
                    [&]( const wxString& aMsg )
                    {
                        errorHandler( footprint, nullptr, DRCE_FOOTPRINT_TYPE_MISMATCH, aMsg,
                                      footprint->GetPosition(), footprint->GetLayer() );
                    } );
        }

        if( !m_drcEngine->IsErrorLimitExceeded( DRCE_PAD_TH_WITH_NO_HOLE )
                || !m_drcEngine->IsErrorLimitExceeded( DRCE_PADSTACK ) )
        {
            footprint->CheckPads(
                    [&]( const PAD* aPad, int aErrorCode, const wxString& aMsg )
                    {
                        if( !m_drcEngine->IsErrorLimitExceeded( aErrorCode ) )
                        {
                            errorHandler( aPad, nullptr, aErrorCode, aMsg, aPad->GetPosition(),
                                          aPad->GetPrincipalLayer() );
                        }
                    } );
        }

        if( !m_drcEngine->IsErrorLimitExceeded( DRCE_OVERLAPPING_PADS ) )
        {
            footprint->CheckOverlappingPads(
                    [&]( const PAD* aPadA, const PAD* aPadB, const VECTOR2I& aPosition )
                    {
                        errorHandler( aPadA, aPadB, DRCE_OVERLAPPING_PADS, wxEmptyString,
                                      aPosition, aPadA->GetPrincipalLayer() );
                    } );
        }
    }

    return !m_drcEngine->IsCancelled();
}


namespace detail
{
static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_FOOTPRINT_CHECKS> dummy;
}
