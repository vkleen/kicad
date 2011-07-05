/**
*  EESchema - libeditframe.cpp
* class LIB_EDIT_FRAME: the component editor frame
*
*/

#include "fctsys.h"
#include "appl_wxstruct.h"
#include "common.h"
#include "macros.h"
#include "class_drawpanel.h"
#include "confirm.h"
#include "eda_doc.h"
#include "bitmaps.h"
#include "gr_basic.h"
#include "class_sch_screen.h"
#include "wxEeschemaStruct.h"

#include "general.h"
#include "protos.h"
#include "eeschema_id.h"
#include "libeditframe.h"
#include "class_library.h"
#include "lib_polyline.h"

#include "kicad_device_context.h"
#include "hotkeys.h"

#include "dialogs/dialog_lib_edit_text.h"
#include "dialogs/dialog_SVG_print.h"
#include "dialogs/dialog_edit_component_in_lib.h"
#include "dialogs/dialog_libedit_dimensions.h"

#include "dialog_helpers.h"

#include <boost/foreach.hpp>


/* Library editor wxConfig entry names. */
const wxString lastLibExportPathEntry( wxT( "LastLibraryExportPath" ) );
const wxString lastLibImportPathEntry( wxT( "LastLibraryImportPath" ) );


/* This method guarantees unique IDs for the library this run of Eeschema
 * which prevents ID conflicts and eliminates the need to recompile every
 * source file in the project when adding IDs to include/id.h. */
int ExportPartId = ::wxNewId();
int ImportPartId = ::wxNewId();
int CreateNewLibAndSavePartId = ::wxNewId();


/*
 * Static component library editor members.  These are static so their
 * state is saved between editing sessions.  This way the last component
 * that was being edited will be displayed.  These members are protected
 * making it necessary to use the class access methods.
 */
LIB_COMPONENT* LIB_EDIT_FRAME::m_component = NULL;
CMP_LIBRARY* LIB_EDIT_FRAME::  m_library   = NULL;

wxString LIB_EDIT_FRAME::      m_aliasName;
int LIB_EDIT_FRAME::           m_unit    = 1;
int LIB_EDIT_FRAME::           m_convert = 1;
LIB_ITEM* LIB_EDIT_FRAME::m_lastDrawItem = NULL;
LIB_ITEM* LIB_EDIT_FRAME::m_drawItem = NULL;
bool LIB_EDIT_FRAME::          m_showDeMorgan    = false;
wxSize LIB_EDIT_FRAME::        m_clientSize      = wxSize( -1, -1 );
int LIB_EDIT_FRAME::           m_textSize        = DEFAULT_SIZE_TEXT;
int LIB_EDIT_FRAME::           m_textOrientation = TEXT_ORIENT_HORIZ;
int LIB_EDIT_FRAME::           m_drawLineWidth   = 0;
FILL_T LIB_EDIT_FRAME::        m_drawFillStyle   = NO_FILL;


/************************/
/* class LIB_EDIT_FRAME */
/************************/
BEGIN_EVENT_TABLE( LIB_EDIT_FRAME, EDA_DRAW_FRAME )
    EVT_CLOSE( LIB_EDIT_FRAME::OnCloseWindow )
    EVT_SIZE( LIB_EDIT_FRAME::OnSize )
    EVT_ACTIVATE( LIB_EDIT_FRAME::OnActivate )

    /* Main horizontal toolbar. */
    EVT_TOOL( ID_LIBEDIT_SAVE_CURRENT_LIB, LIB_EDIT_FRAME::SaveActiveLibrary )
    EVT_TOOL( ID_LIBEDIT_SELECT_CURRENT_LIB, LIB_EDIT_FRAME::Process_Special_Functions )
    EVT_TOOL( ID_LIBEDIT_DELETE_PART, LIB_EDIT_FRAME::DeleteOnePart )
    EVT_TOOL( ID_LIBEDIT_NEW_PART, LIB_EDIT_FRAME::CreateNewLibraryPart )
    EVT_TOOL( ID_LIBEDIT_NEW_PART_FROM_EXISTING, LIB_EDIT_FRAME::OnCreateNewPartFromExisting )

    EVT_TOOL( ID_LIBEDIT_SELECT_PART, LIB_EDIT_FRAME::LoadOneLibraryPart )
    EVT_TOOL( ID_LIBEDIT_SAVE_CURRENT_PART, LIB_EDIT_FRAME::Process_Special_Functions )
    EVT_TOOL( wxID_UNDO, LIB_EDIT_FRAME::GetComponentFromUndoList )
    EVT_TOOL( wxID_REDO, LIB_EDIT_FRAME::GetComponentFromRedoList )
    EVT_TOOL( ID_LIBEDIT_GET_FRAME_EDIT_PART, LIB_EDIT_FRAME::OnEditComponentProperties )
    EVT_TOOL( ID_LIBEDIT_GET_FRAME_EDIT_FIELDS, LIB_EDIT_FRAME::InstallFieldsEditorDialog )
    EVT_TOOL( ID_LIBEDIT_CHECK_PART, LIB_EDIT_FRAME::OnCheckComponent )
    EVT_TOOL( ID_DE_MORGAN_NORMAL_BUTT, LIB_EDIT_FRAME::OnSelectBodyStyle )
    EVT_TOOL( ID_DE_MORGAN_CONVERT_BUTT, LIB_EDIT_FRAME::OnSelectBodyStyle )
    EVT_TOOL( ID_LIBEDIT_VIEW_DOC, LIB_EDIT_FRAME::OnViewEntryDoc )
    EVT_TOOL( ID_LIBEDIT_EDIT_PIN_BY_PIN, LIB_EDIT_FRAME::Process_Special_Functions )
    EVT_TOOL( ExportPartId, LIB_EDIT_FRAME::OnExportPart )
    EVT_TOOL( CreateNewLibAndSavePartId, LIB_EDIT_FRAME::OnExportPart )
    EVT_TOOL( ImportPartId, LIB_EDIT_FRAME::OnImportPart )

    EVT_COMBOBOX( ID_LIBEDIT_SELECT_PART_NUMBER, LIB_EDIT_FRAME::OnSelectPart )
    EVT_COMBOBOX( ID_LIBEDIT_SELECT_ALIAS, LIB_EDIT_FRAME::OnSelectAlias )

    /* Right vertical toolbar. */
    EVT_TOOL( ID_NO_TOOL_SELECTED, LIB_EDIT_FRAME::OnSelectTool )
    EVT_TOOL_RANGE( ID_LIBEDIT_PIN_BUTT, ID_LIBEDIT_DELETE_ITEM_BUTT,
                    LIB_EDIT_FRAME::OnSelectTool )

    /* menubar commands */
    EVT_MENU( wxID_EXIT, LIB_EDIT_FRAME::CloseWindow )
    EVT_MENU( ID_LIBEDIT_SAVE_CURRENT_LIB_AS, LIB_EDIT_FRAME::SaveActiveLibrary )
    EVT_MENU( ID_LIBEDIT_GEN_PNG_FILE, LIB_EDIT_FRAME::OnPlotCurrentComponent )
    EVT_MENU( ID_LIBEDIT_GEN_SVG_FILE, LIB_EDIT_FRAME::OnPlotCurrentComponent )
    EVT_MENU( wxID_HELP, EDA_DRAW_FRAME::GetKicadHelp )
    EVT_MENU( wxID_ABOUT, EDA_BASE_FRAME::GetKicadAbout )

    EVT_MENU( ID_COLORS_SETUP, LIB_EDIT_FRAME::OnColorConfig )
    EVT_MENU( ID_CONFIG_REQ, LIB_EDIT_FRAME::InstallConfigFrame )
    EVT_MENU( ID_CONFIG_SAVE, LIB_EDIT_FRAME::Process_Config )
    EVT_MENU( ID_CONFIG_READ, LIB_EDIT_FRAME::Process_Config )
    EVT_MENU( ID_COLORS_SETUP, LIB_EDIT_FRAME::Process_Config )
    EVT_MENU( ID_LIBEDIT_DIMENSIONS, LIB_EDIT_FRAME::InstallDimensionsDialog )

    // Multple item selection context menu commands.
    EVT_MENU_RANGE( ID_SELECT_ITEM_START, ID_SELECT_ITEM_END, LIB_EDIT_FRAME::OnSelectItem )

    EVT_MENU_RANGE( ID_PREFERENCES_HOTKEY_START, ID_PREFERENCES_HOTKEY_END,
                    LIB_EDIT_FRAME::Process_Config )

    EVT_MENU_RANGE( ID_LANGUAGE_CHOICE, ID_LANGUAGE_CHOICE_END, LIB_EDIT_FRAME::SetLanguage )

    /* Context menu events and commands. */
    EVT_MENU( ID_LIBEDIT_EDIT_PIN, LIB_EDIT_FRAME::OnEditPin )
    EVT_MENU( ID_LIBEDIT_ROTATE_ITEM, LIB_EDIT_FRAME::OnRotateItem )

    EVT_MENU_RANGE( ID_POPUP_LIBEDIT_PIN_GLOBAL_CHANGE_ITEM,
                    ID_POPUP_LIBEDIT_DELETE_CURRENT_POLY_SEGMENT,
                    LIB_EDIT_FRAME::Process_Special_Functions )

    EVT_MENU_RANGE( ID_POPUP_GENERAL_START_RANGE, ID_POPUP_GENERAL_END_RANGE,
                    LIB_EDIT_FRAME::Process_Special_Functions )

   /* Update user interface elements. */
    EVT_UPDATE_UI( ExportPartId, LIB_EDIT_FRAME::OnUpdateEditingPart )
    EVT_UPDATE_UI( CreateNewLibAndSavePartId, LIB_EDIT_FRAME::OnUpdateEditingPart )
    EVT_UPDATE_UI( ID_LIBEDIT_SAVE_CURRENT_PART, LIB_EDIT_FRAME::OnUpdateEditingPart )
    EVT_UPDATE_UI( ID_LIBEDIT_GET_FRAME_EDIT_FIELDS, LIB_EDIT_FRAME::OnUpdateEditingPart )
    EVT_UPDATE_UI( ID_LIBEDIT_CHECK_PART, LIB_EDIT_FRAME::OnUpdateEditingPart )
    EVT_UPDATE_UI( ID_LIBEDIT_GET_FRAME_EDIT_PART, LIB_EDIT_FRAME::OnUpdateEditingPart )
    EVT_UPDATE_UI( ID_LIBEDIT_NEW_PART_FROM_EXISTING, LIB_EDIT_FRAME::OnUpdateEditingPart )
    EVT_UPDATE_UI( wxID_UNDO, LIB_EDIT_FRAME::OnUpdateUndo )
    EVT_UPDATE_UI( wxID_REDO, LIB_EDIT_FRAME::OnUpdateRedo )
    EVT_UPDATE_UI( ID_LIBEDIT_SAVE_CURRENT_LIB, LIB_EDIT_FRAME::OnUpdateSaveCurrentLib )
    EVT_UPDATE_UI( ID_LIBEDIT_VIEW_DOC, LIB_EDIT_FRAME::OnUpdateViewDoc )
    EVT_UPDATE_UI( ID_LIBEDIT_EDIT_PIN_BY_PIN, LIB_EDIT_FRAME::OnUpdatePinByPin )
    EVT_UPDATE_UI( ID_LIBEDIT_SELECT_PART_NUMBER, LIB_EDIT_FRAME::OnUpdatePartNumber )
    EVT_UPDATE_UI( ID_LIBEDIT_SELECT_ALIAS, LIB_EDIT_FRAME::OnUpdateSelectAlias )
    EVT_UPDATE_UI( ID_DE_MORGAN_NORMAL_BUTT, LIB_EDIT_FRAME::OnUpdateDeMorganNormal )
    EVT_UPDATE_UI( ID_DE_MORGAN_CONVERT_BUTT, LIB_EDIT_FRAME::OnUpdateDeMorganConvert )
    EVT_UPDATE_UI( ID_NO_TOOL_SELECTED, LIB_EDIT_FRAME::OnUpdateEditingPart )
    EVT_UPDATE_UI_RANGE( ID_LIBEDIT_PIN_BUTT, ID_LIBEDIT_DELETE_ITEM_BUTT,
                         LIB_EDIT_FRAME::OnUpdateEditingPart )
END_EVENT_TABLE()


LIB_EDIT_FRAME::LIB_EDIT_FRAME( SCH_EDIT_FRAME* aParent,
                                const wxString& title,
                                const wxPoint&  pos,
                                const wxSize&   size,
                                long            style ) :
    EDA_DRAW_FRAME( aParent, LIBEDITOR_FRAME, title, pos, size, style )
{
    wxASSERT( aParent );

    m_FrameName  = wxT( "LibeditFrame" );
    m_Draw_Axis  = true;            // true to draw axis
    m_ConfigPath = wxT( "LibraryEditor" );
    SetShowDeMorgan( false );
    m_drawSpecificConvert = true;
    m_drawSpecificUnit    = false;
    m_tempCopyComponent   = NULL;
    m_HotkeysZoomAndGridList = s_Libedit_Hokeys_Descr;

    // Give an icon
    SetIcon( wxIcon( libedit_xpm ) );
    SetScreen( new SCH_SCREEN() );
    GetScreen()->m_Center = true;
    GetScreen()->SetCrossHairPosition( wxPoint( 0, 0 ) );

    LoadSettings();

    // Initilialize grid id to a default value if not found in config or bad:
    if( (m_LastGridSizeId <= 0)
       || ( m_LastGridSizeId < (ID_POPUP_GRID_USER - ID_POPUP_GRID_LEVEL_1000) ) )
        m_LastGridSizeId = ID_POPUP_GRID_LEVEL_50 - ID_POPUP_GRID_LEVEL_1000;

    SetSize( m_FramePos.x, m_FramePos.y, m_FrameSize.x, m_FrameSize.y );
    GetScreen()->SetGrid( ID_POPUP_GRID_LEVEL_1000 + m_LastGridSizeId  );

    if( DrawPanel )
        DrawPanel->m_Block_Enable = true;

    EnsureActiveLibExists();
    ReCreateMenuBar();
    ReCreateHToolbar();
    ReCreateVToolbar();
    CreateOptionToolbar();
    DisplayLibInfos();
    DisplayCmpDoc();
    UpdateAliasSelectList();
    UpdatePartSelectList();

    m_auimgr.SetManagedWindow( this );

    wxAuiPaneInfo horiz;
    horiz.Gripper( false );
    horiz.DockFixed( true );
    horiz.Movable( false );
    horiz.Floatable( false );
    horiz.CloseButton( false );
    horiz.CaptionVisible( false );

    wxAuiPaneInfo vert( horiz );

    vert.TopDockable( false ).BottomDockable( false );
    horiz.LeftDockable( false ).RightDockable( false );

    m_auimgr.AddPane( m_HToolBar,
                      wxAuiPaneInfo( horiz ).Name( wxT( "m_HToolBar" ) ).Top().Row( 0 ) );

    m_auimgr.AddPane( m_VToolBar,
                      wxAuiPaneInfo( vert ).Name( wxT( "m_VToolBar" ) ).Right() );

    m_auimgr.AddPane( m_OptionsToolBar,
                      wxAuiPaneInfo( vert ).Name( wxT( "m_OptionsToolBar" ) ).Left() );

    m_auimgr.AddPane( DrawPanel,
                      wxAuiPaneInfo().Name( wxT( "DrawFrame" ) ).CentrePane() );

    m_auimgr.AddPane( MsgPanel,
                      wxAuiPaneInfo( horiz ).Name( wxT( "MsgPanel" ) ).Bottom() );

    m_auimgr.Update();

    Show( true );

    wxCommandEvent evt( wxEVT_COMMAND_MENU_SELECTED, ID_ZOOM_PAGE );
    wxPostEvent( this, evt );
}


LIB_EDIT_FRAME::~LIB_EDIT_FRAME()
{
    SCH_EDIT_FRAME* frame = (SCH_EDIT_FRAME*) wxGetApp().GetTopWindow();

    frame->m_LibeditFrame = NULL;
    m_drawItem = m_lastDrawItem = NULL;

    if ( m_tempCopyComponent )
        delete m_tempCopyComponent;

    m_tempCopyComponent = NULL;
}


/**
 * Load library editor frame specific configuration settings.
 *
 * Don't forget to call this base method from any derived classes or the
 * settings will not get loaded.
 */
void LIB_EDIT_FRAME::LoadSettings()
{
    wxConfig* cfg;

    EDA_DRAW_FRAME::LoadSettings();

    wxConfigPathChanger cpc( wxGetApp().m_EDA_Config, m_ConfigPath );
    cfg = wxGetApp().m_EDA_Config;

    m_LastLibExportPath = cfg->Read( lastLibExportPathEntry, ::wxGetCwd() );
    m_LastLibImportPath = cfg->Read( lastLibImportPathEntry, ::wxGetCwd() );
}


void LIB_EDIT_FRAME::SetDrawItem( LIB_ITEM* drawItem )
{
    m_drawItem = drawItem;
}


/**
 * Save library editor frame specific configuration settings.
 *
 * Don't forget to call this base method from any derived classes or the
 * settings will not get saved.
 */
void LIB_EDIT_FRAME::SaveSettings()
{
    wxConfig* cfg;

    EDA_DRAW_FRAME::SaveSettings();

    wxConfigPathChanger cpc( wxGetApp().m_EDA_Config, m_ConfigPath );
    cfg = wxGetApp().m_EDA_Config;

    cfg->Write( lastLibExportPathEntry, m_LastLibExportPath );
    cfg->Write( lastLibImportPathEntry, m_LastLibImportPath );
}


void LIB_EDIT_FRAME::OnCloseWindow( wxCloseEvent& Event )
{
    if( GetScreen()->IsModify() )
    {
        if( !IsOK( this, _( "Component was modified!\nDiscard changes?" ) ) )
        {
            Event.Veto();
            return;
        }
        else
            GetScreen()->ClrModify();
    }

    BOOST_FOREACH( const CMP_LIBRARY &lib, CMP_LIBRARY::GetLibraryList() )
    {
        if( lib.IsModified() )
        {
            wxString msg;
            msg.Printf( _( "Library \"%s\" was modified!\nDiscard changes?" ),
                        GetChars( lib.GetName() ) );

            if( !IsOK( this, msg ) )
            {
                Event.Veto();
                return;
            }
        }
    }

    SaveSettings();
    Destroy();
}


double LIB_EDIT_FRAME::BestZoom()
{
/* Please, note: wxMSW before version 2.9 seems have
 * problems with zoom values < 1 ( i.e. userscale > 1) and needs to be patched:
 * edit file <wxWidgets>/src/msw/dc.cpp
 * search for line static const int VIEWPORT_EXTENT = 1000;
 * and replace by static const int VIEWPORT_EXTENT = 10000;
 */
    int      dx, dy;
    wxSize   size;
    EDA_RECT BoundaryBox;

    if( m_component )
    {
        BoundaryBox = m_component->GetBoundingBox( m_unit, m_convert );
        dx = BoundaryBox.GetWidth();
        dy = BoundaryBox.GetHeight();
        GetScreen()->SetScrollCenterPosition( wxPoint( 0, 0 )
        );
    }
    else
    {
        dx = GetScreen()->m_CurrentSheetDesc->m_Size.x;
        dy = GetScreen()->m_CurrentSheetDesc->m_Size.y;
        GetScreen()->SetScrollCenterPosition( wxPoint( 0, 0 ) );
    }

    size = DrawPanel->GetClientSize();

    // Reserve a 10% margin around component bounding box.
    double margin_scale_factor = 0.8;
    double zx =(double) dx / (margin_scale_factor * (double)size.x );
    double zy = (double) dy / ( margin_scale_factor * (double)size.y );

    double bestzoom = MAX( zx, zy );

    // keep it >= minimal existing zoom (can happen for very small components
    // for instance when starting a new component
    if( bestzoom  < GetScreen()->m_ZoomList[0] )
        bestzoom  = GetScreen()->m_ZoomList[0];

    return bestzoom;
}


void LIB_EDIT_FRAME::UpdateAliasSelectList()
{
    if( m_SelAliasBox == NULL )
        return;

    m_SelAliasBox->Clear();

    if( m_component == NULL )
        return;

    m_SelAliasBox->Append( m_component->GetAliasNames() );
    m_SelAliasBox->SetSelection( 0 );

    int index = m_SelAliasBox->FindString( m_aliasName );

    if( index != wxNOT_FOUND )
        m_SelAliasBox->SetSelection( index );
}


void LIB_EDIT_FRAME::UpdatePartSelectList()
{
    if( m_SelpartBox == NULL )
        return;

    if( m_SelpartBox->GetCount() != 0 )
        m_SelpartBox->Clear();

    if( m_component == NULL || m_component->GetPartCount() <= 1 )
    {
        m_SelpartBox->Append( wxEmptyString );
    }
    else
    {
        for( int i = 0; i < m_component->GetPartCount(); i++ )
        {
            wxString msg;
            msg.Printf( _( "Part %c" ), 'A' + i );
            m_SelpartBox->Append( msg );
        }
    }

    m_SelpartBox->SetSelection( ( m_unit > 0 ) ? m_unit - 1 : 0 );
}


void LIB_EDIT_FRAME::OnUpdateEditingPart( wxUpdateUIEvent& aEvent )
{
    aEvent.Enable( m_component != NULL );

    if( m_component != NULL && aEvent.GetEventObject() == m_VToolBar )
        aEvent.Check( GetToolId() == aEvent.GetId() );
}


void LIB_EDIT_FRAME::OnUpdateNotEditingPart( wxUpdateUIEvent& event )
{
    event.Enable( m_component == NULL );
}


void LIB_EDIT_FRAME::OnUpdateUndo( wxUpdateUIEvent& event )
{
    event.Enable( m_component != NULL && GetScreen() != NULL
                  && GetScreen()->GetUndoCommandCount() != 0 && !IsEditingDrawItem() );
}


void LIB_EDIT_FRAME::OnUpdateRedo( wxUpdateUIEvent& event )
{
    event.Enable( m_component != NULL && GetScreen() != NULL
                  && GetScreen()->GetRedoCommandCount() != 0 && !IsEditingDrawItem() );
}


void LIB_EDIT_FRAME::OnUpdateSaveCurrentLib( wxUpdateUIEvent& event )
{
    event.Enable( m_library != NULL && ( m_library->IsModified() || GetScreen()->IsModify() ) );
}


void LIB_EDIT_FRAME::OnUpdateViewDoc( wxUpdateUIEvent& event )
{
    bool enable = false;

    if( m_component != NULL && m_library != NULL )
    {
        LIB_ALIAS* alias = m_component->GetAlias( m_aliasName );

        wxCHECK_RET( alias != NULL, wxT( "Alias <" ) + m_aliasName + wxT( "> not found." ) );

        enable = !alias->GetDocFileName().IsEmpty();
    }

    event.Enable( enable );
}


void LIB_EDIT_FRAME::OnUpdatePinByPin( wxUpdateUIEvent& event )
{
    event.Enable( ( m_component != NULL )
                 && ( ( m_component->GetPartCount() > 1 ) || m_showDeMorgan ) );

    event.Check( g_EditPinByPinIsOn );
}


void LIB_EDIT_FRAME::OnUpdatePartNumber( wxUpdateUIEvent& event )
{
    if( m_SelpartBox == NULL )
        return;

    /* Using the typical event.Enable() call doesn't seem to work with wxGTK
     * so use the pointer to alias combobox to directly enable or disable.
     */
    m_SelpartBox->Enable( m_component && m_component->GetPartCount() > 1 );
}


void LIB_EDIT_FRAME::OnUpdateDeMorganNormal( wxUpdateUIEvent& event )
{
    if( m_HToolBar == NULL )
        return;

    event.Enable( GetShowDeMorgan() || ( m_component && m_component->HasConversion() ) );
    event.Check( m_convert <= 1 );
}


void LIB_EDIT_FRAME::OnUpdateDeMorganConvert( wxUpdateUIEvent& event )
{
    if( m_HToolBar == NULL )
        return;

    event.Enable( GetShowDeMorgan() || ( m_component && m_component->HasConversion() ) );
    event.Check( m_convert > 1 );
}


void LIB_EDIT_FRAME::OnUpdateSelectAlias( wxUpdateUIEvent& event )
{
    if( m_SelAliasBox == NULL )
        return;

    /* Using the typical event.Enable() call doesn't seem to work with wxGTK
     * so use the pointer to alias combobox to directly enable or disable.
     */
    m_SelAliasBox->Enable( m_component != NULL && m_component->GetAliasCount() > 1 );
}


void LIB_EDIT_FRAME::OnSelectAlias( wxCommandEvent& event )
{
    if( m_SelAliasBox == NULL
        || ( m_SelAliasBox->GetStringSelection().CmpNoCase( m_aliasName ) == 0)  )
        return;

    m_lastDrawItem = NULL;
    m_aliasName = m_SelAliasBox->GetStringSelection();

    DisplayCmpDoc();
    DrawPanel->Refresh();
}


void LIB_EDIT_FRAME::OnSelectPart( wxCommandEvent& event )
{
    int i = event.GetSelection();

    if( ( i == wxNOT_FOUND ) || ( ( i + 1 ) == m_unit ) )
        return;

    m_lastDrawItem = NULL;
    m_unit = i + 1;
    DrawPanel->Refresh();
    DisplayCmpDoc();
}


void LIB_EDIT_FRAME::OnViewEntryDoc( wxCommandEvent& event )
{
    if( m_component == NULL )
        return;

    wxString fileName;
    LIB_ALIAS* alias = m_component->GetAlias( m_aliasName );

    wxCHECK_RET( alias != NULL, wxT( "Alias not found." ) );

    fileName = alias->GetDocFileName();

    if( !fileName.IsEmpty() )
        GetAssociatedDocument( this, fileName, &wxGetApp().GetLibraryPathList() );
}


void LIB_EDIT_FRAME::OnSelectBodyStyle( wxCommandEvent& event )
{
    DrawPanel->EndMouseCapture( ID_NO_TOOL_SELECTED, DrawPanel->GetDefaultCursor() );

    if( event.GetId() == ID_DE_MORGAN_NORMAL_BUTT )
        m_convert = 1;
    else
        m_convert = 2;

    m_lastDrawItem = NULL;
    DrawPanel->Refresh();
}


void LIB_EDIT_FRAME::Process_Special_Functions( wxCommandEvent& event )
{
    int     id = event.GetId();
    wxPoint pos;

    DrawPanel->m_IgnoreMouseEvents = true;

    wxGetMousePosition( &pos.x, &pos.y );
    pos.y += 20;

    switch( id )   // Stop placement commands before handling new command.
    {
    case ID_POPUP_LIBEDIT_END_CREATE_ITEM:
    case ID_LIBEDIT_EDIT_PIN:
    case ID_POPUP_LIBEDIT_BODY_EDIT_ITEM:
    case ID_POPUP_LIBEDIT_FIELD_EDIT_ITEM:
    case ID_POPUP_LIBEDIT_PIN_GLOBAL_CHANGE_PINSIZE_ITEM:
    case ID_POPUP_LIBEDIT_PIN_GLOBAL_CHANGE_PINNAMESIZE_ITEM:
    case ID_POPUP_LIBEDIT_PIN_GLOBAL_CHANGE_PINNUMSIZE_ITEM:
    case ID_POPUP_ZOOM_BLOCK:
    case ID_POPUP_DELETE_BLOCK:
    case ID_POPUP_COPY_BLOCK:
    case ID_POPUP_SELECT_ITEMS_BLOCK:
    case ID_POPUP_MIRROR_X_BLOCK:
    case ID_POPUP_MIRROR_Y_BLOCK:
    case ID_POPUP_ROTATE_BLOCK:
    case ID_POPUP_PLACE_BLOCK:
    case ID_POPUP_LIBEDIT_DELETE_CURRENT_POLY_SEGMENT:
        break;

    case ID_POPUP_LIBEDIT_CANCEL_EDITING:
        if( DrawPanel->IsMouseCaptured() )
            DrawPanel->EndMouseCapture();
        else
            DrawPanel->EndMouseCapture( ID_NO_TOOL_SELECTED, DrawPanel->GetDefaultCursor() );
        break;

    case ID_POPUP_LIBEDIT_DELETE_ITEM:
        DrawPanel->EndMouseCapture();
        break;

    default:
        DrawPanel->EndMouseCapture( ID_NO_TOOL_SELECTED, DrawPanel->GetDefaultCursor(),
                                    wxEmptyString );
        break;
    }

    INSTALL_UNBUFFERED_DC( dc, DrawPanel );

    switch( id )
    {
    case ID_POPUP_LIBEDIT_CANCEL_EDITING:
        break;

    case ID_LIBEDIT_SELECT_CURRENT_LIB:
        SelectActiveLibrary();
        break;

    case ID_LIBEDIT_SAVE_CURRENT_PART:
        SaveOnePartInMemory();
        break;

    case ID_LIBEDIT_EDIT_PIN_BY_PIN:
        g_EditPinByPinIsOn = m_HToolBar->GetToolState( ID_LIBEDIT_EDIT_PIN_BY_PIN );
        break;

    case ID_POPUP_LIBEDIT_END_CREATE_ITEM:
        DrawPanel->MoveCursorToCrossHair();
        if( m_drawItem )
        {
            EndDrawGraphicItem( &dc );
        }
        break;

    case ID_POPUP_LIBEDIT_BODY_EDIT_ITEM:
        if( m_drawItem )
        {
            DrawPanel->CrossHairOff( &dc );

            switch( m_drawItem->Type() )
            {
            case LIB_ARC_T:
            case LIB_CIRCLE_T:
            case LIB_RECTANGLE_T:
            case LIB_POLYLINE_T:
                EditGraphicSymbol( &dc, m_drawItem );
                break;

            case LIB_TEXT_T:
                EditSymbolText( &dc, m_drawItem );
                break;

            default:
                ;
            }

            DrawPanel->CrossHairOn( &dc );
        }
        break;

    case ID_POPUP_LIBEDIT_DELETE_CURRENT_POLY_SEGMENT:
    {
        // Delete the last created segment, while creating a polyline draw item
        if( m_drawItem == NULL )
            break;

        DrawPanel->MoveCursorToCrossHair();
        int oldFlags = m_drawItem->GetFlags();
        m_drawItem->ClearFlags();
        m_drawItem->Draw( DrawPanel, &dc, wxPoint( 0, 0 ), -1, g_XorMode, NULL, DefaultTransform );
        ( (LIB_POLYLINE*) m_drawItem )->DeleteSegment( GetScreen()->GetCrossHairPosition( true ) );
        m_drawItem->Draw( DrawPanel, &dc, wxPoint( 0, 0 ), -1, g_XorMode, NULL, DefaultTransform );
        m_drawItem->SetFlags( oldFlags );
        break;
    }

    case ID_POPUP_LIBEDIT_DELETE_ITEM:
        if( m_drawItem )
            deleteItem( &dc );

        break;

    case ID_POPUP_LIBEDIT_MOVE_ITEM_REQUEST:
        if( m_drawItem == NULL )
            break;

        if( m_drawItem->Type() == LIB_PIN_T )
            StartMovePin( &dc );
        else
            StartMoveDrawSymbol( &dc );
        break;

    case ID_POPUP_LIBEDIT_MODIFY_ITEM:

        if( m_drawItem == NULL )
            break;

        DrawPanel->MoveCursorToCrossHair();
        if( m_drawItem->Type() == LIB_RECTANGLE_T
            || m_drawItem->Type() == LIB_CIRCLE_T
            || m_drawItem->Type() == LIB_POLYLINE_T
            || m_drawItem->Type() == LIB_ARC_T
            )
        {
            StartModifyDrawSymbol( &dc );
        }

        break;

    case ID_POPUP_LIBEDIT_FIELD_EDIT_ITEM:
        if( m_drawItem == NULL )
            break;
        DrawPanel->CrossHairOff( &dc );
        if( m_drawItem->Type() == LIB_FIELD_T )
        {
            EditField( &dc, (LIB_FIELD*) m_drawItem );
        }
        DrawPanel->MoveCursorToCrossHair();
        DrawPanel->CrossHairOn( &dc );
        break;

    case ID_POPUP_LIBEDIT_PIN_GLOBAL_CHANGE_PINSIZE_ITEM:
    case ID_POPUP_LIBEDIT_PIN_GLOBAL_CHANGE_PINNAMESIZE_ITEM:
    case ID_POPUP_LIBEDIT_PIN_GLOBAL_CHANGE_PINNUMSIZE_ITEM:
        if( ( m_drawItem == NULL ) || ( m_drawItem->Type() != LIB_PIN_T ) )
            break;
        SaveCopyInUndoList( m_component );
        GlobalSetPins( &dc, (LIB_PIN*) m_drawItem, id );
        DrawPanel->MoveCursorToCrossHair();
        break;

    case ID_POPUP_ZOOM_BLOCK:
        DrawPanel->m_AutoPAN_Request = false;
        GetScreen()->m_BlockLocate.m_Command = BLOCK_ZOOM;
        HandleBlockEnd( &dc );
        break;

    case ID_POPUP_DELETE_BLOCK:
        DrawPanel->m_AutoPAN_Request = false;
        GetScreen()->m_BlockLocate.m_Command = BLOCK_DELETE;
        DrawPanel->MoveCursorToCrossHair();
        HandleBlockEnd( &dc );
        break;

    case ID_POPUP_COPY_BLOCK:
        DrawPanel->m_AutoPAN_Request = false;
        GetScreen()->m_BlockLocate.m_Command = BLOCK_COPY;
        DrawPanel->MoveCursorToCrossHair();
        HandleBlockPlace( &dc );
        break;

    case ID_POPUP_SELECT_ITEMS_BLOCK:
        DrawPanel->m_AutoPAN_Request = false;
        GetScreen()->m_BlockLocate.m_Command = BLOCK_SELECT_ITEMS_ONLY;
        DrawPanel->MoveCursorToCrossHair();
        HandleBlockEnd( &dc );
        break;

    case ID_POPUP_MIRROR_Y_BLOCK:
        DrawPanel->m_AutoPAN_Request = false;
        GetScreen()->m_BlockLocate.m_Command = BLOCK_MIRROR_Y;
        DrawPanel->MoveCursorToCrossHair();
        HandleBlockPlace( &dc );
        break;

    case ID_POPUP_MIRROR_X_BLOCK:
        DrawPanel->m_AutoPAN_Request = false;
        GetScreen()->m_BlockLocate.m_Command = BLOCK_MIRROR_X;
        DrawPanel->MoveCursorToCrossHair();
        HandleBlockPlace( &dc );
        break;

    case ID_POPUP_ROTATE_BLOCK:
        DrawPanel->m_AutoPAN_Request = false;
        GetScreen()->m_BlockLocate.m_Command = BLOCK_ROTATE;
        DrawPanel->MoveCursorToCrossHair();
        HandleBlockPlace( &dc );
        break;

    case ID_POPUP_PLACE_BLOCK:
        DrawPanel->m_AutoPAN_Request = false;
        DrawPanel->MoveCursorToCrossHair();
        HandleBlockPlace( &dc );
        break;

    default:
        DisplayError( this, wxT( "LIB_EDIT_FRAME::Process_Special_Functions error" ) );
        break;
    }

    DrawPanel->m_IgnoreMouseEvents = false;

    if( GetToolId() == ID_NO_TOOL_SELECTED )
        m_lastDrawItem = NULL;
}


void LIB_EDIT_FRAME::OnActivate( wxActivateEvent& event )
{
    EDA_DRAW_FRAME::OnActivate( event );

    // Verify the existence of the current active library
    // (can be removed or changed by the schematic editor)
    EnsureActiveLibExists();
}


void LIB_EDIT_FRAME::EnsureActiveLibExists()
{
    if( m_library == NULL )
        return;

    bool exists = CMP_LIBRARY::LibraryExists( m_library );
    if( exists )
        return;
    else
        m_library = NULL;
}


void LIB_EDIT_FRAME::SetLanguage( wxCommandEvent& event )
{
    EDA_BASE_FRAME::SetLanguage( event );
    SCH_EDIT_FRAME *parent = (SCH_EDIT_FRAME *)GetParent();
    // Call parent->EDA_BASE_FRAME::SetLanguage and NOT
    // parent->SetLanguage because parent->SetLanguage call
    // LIB_EDIT_FRAME::SetLanguage
    parent->EDA_BASE_FRAME::SetLanguage( event );
}


/**
 * Function TempCopyComponent
 * create a temporary copy of the current edited component
 * Used to prepare an Undo ant/or abort command before editing the component
 */
void LIB_EDIT_FRAME::TempCopyComponent()
{
    if( m_tempCopyComponent )
        delete m_tempCopyComponent;

    m_tempCopyComponent = NULL;

    if( m_component )
        m_tempCopyComponent = new LIB_COMPONENT( *m_component );
}

/**
 * Function RestoreComponent
 * Restore the current edited component from its temporary copy.
 * Used to abort a command
 */
void LIB_EDIT_FRAME::RestoreComponent()
{
    if( m_tempCopyComponent == NULL )
        return;

    if( m_component )
        delete m_component;

    m_component = m_tempCopyComponent;
    m_tempCopyComponent = NULL;
}

/**
 * Function ClearTempCopyComponent
 * delete temporary copy of the current component and clear pointer
 */
void LIB_EDIT_FRAME::ClearTempCopyComponent()
{
    delete m_tempCopyComponent;
    m_tempCopyComponent = NULL;
}


/* Creates the SVG print file for the current edited component.
 */
void LIB_EDIT_FRAME::SVG_Print_Component( const wxString& FullFileName )
{
    DIALOG_SVG_PRINT::DrawSVGPage( this, FullFileName, GetScreen() );
}


void LIB_EDIT_FRAME::EditSymbolText( wxDC* DC, LIB_ITEM* DrawItem )
{
    if ( ( DrawItem == NULL ) || ( DrawItem->Type() != LIB_TEXT_T ) )
        return;

    /* Deleting old text. */
    if( DC && !DrawItem->InEditMode() )
        DrawItem->Draw( DrawPanel, DC, wxPoint( 0, 0 ), -1, g_XorMode, NULL, DefaultTransform );

    DIALOG_LIB_EDIT_TEXT* frame = new DIALOG_LIB_EDIT_TEXT( this, (LIB_TEXT*) DrawItem );
    frame->ShowModal();
    frame->Destroy();
    OnModify();

    /* Display new text. */
    if( DC && !DrawItem->InEditMode() )
        DrawItem->Draw( DrawPanel, DC, wxPoint( 0, 0 ), -1, GR_DEFAULT_DRAWMODE, NULL,
                        DefaultTransform );
}


void LIB_EDIT_FRAME::OnEditComponentProperties( wxCommandEvent& event )
{
    bool partLocked = GetComponent()->UnitsLocked();

    DIALOG_EDIT_COMPONENT_IN_LIBRARY dlg( this );

    if( dlg.ShowModal() == wxID_CANCEL )
        return;

    if( partLocked != GetComponent()->UnitsLocked() )
    {
        // g_EditPinByPinIsOn is set to the better value, if m_UnitSelectionLocked has changed
        g_EditPinByPinIsOn = GetComponent()->UnitsLocked() ? true : false;
    }

    UpdateAliasSelectList();
    UpdatePartSelectList();
    DisplayLibInfos();
    DisplayCmpDoc();
    OnModify();
    DrawPanel->Refresh();
}


void LIB_EDIT_FRAME::InstallDimensionsDialog( wxCommandEvent& event )
{
    DIALOG_LIBEDIT_DIMENSIONS dlg( this );
    dlg.ShowModal();
}


void LIB_EDIT_FRAME::OnCreateNewPartFromExisting( wxCommandEvent& event )
{
    wxCHECK_RET( m_component != NULL,
                 wxT( "Cannot create new part from non-existant current part." ) );

    INSTALL_UNBUFFERED_DC( dc, DrawPanel );
    DrawPanel->CrossHairOff( &dc );
    EditField( &dc, &m_component->GetValueField() );
    DrawPanel->MoveCursorToCrossHair();
    DrawPanel->CrossHairOn( &dc );
}

void LIB_EDIT_FRAME::OnSelectTool( wxCommandEvent& aEvent )
{
    int id = aEvent.GetId();

    if( GetToolId() == ID_NO_TOOL_SELECTED )
        m_lastDrawItem = NULL;

    DrawPanel->EndMouseCapture( ID_NO_TOOL_SELECTED, DrawPanel->GetDefaultCursor(),
                                wxEmptyString );

    switch( id )
    {
    case ID_NO_TOOL_SELECTED:
        SetToolID( id, DrawPanel->GetDefaultCursor(), wxEmptyString );
        break;

    case ID_LIBEDIT_PIN_BUTT:
        if( m_component )
        {
            SetToolID( id, wxCURSOR_PENCIL, _( "Add pin" ) );
        }
        else
        {
            SetToolID( id, wxCURSOR_ARROW, _( "Set pin options" ) );
            wxCommandEvent cmd( wxEVT_COMMAND_MENU_SELECTED );
            cmd.SetId( ID_LIBEDIT_EDIT_PIN );
            GetEventHandler()->ProcessEvent( cmd );
            SetToolID( ID_NO_TOOL_SELECTED, DrawPanel->GetDefaultCursor(), wxEmptyString );
        }
        break;

    case ID_LIBEDIT_BODY_TEXT_BUTT:
        SetToolID( id, wxCURSOR_PENCIL, _( "Add text" ) );
        break;

    case ID_LIBEDIT_BODY_RECT_BUTT:
        SetToolID( id, wxCURSOR_PENCIL, _( "Add rectangle" ) );
        break;

    case ID_LIBEDIT_BODY_CIRCLE_BUTT:
        SetToolID( id, wxCURSOR_PENCIL, _( "Add circle" ) );
        break;

    case ID_LIBEDIT_BODY_ARC_BUTT:
        SetToolID( id, wxCURSOR_PENCIL, _( "Add arc" ) );
        break;

    case ID_LIBEDIT_BODY_LINE_BUTT:
        SetToolID( id, wxCURSOR_PENCIL, _( "Add line" ) );
        break;

    case ID_LIBEDIT_ANCHOR_ITEM_BUTT:
        SetToolID( id, wxCURSOR_HAND, _( "Set anchor position" ) );
        break;

    case ID_LIBEDIT_IMPORT_BODY_BUTT:
        SetToolID( id, DrawPanel->GetDefaultCursor(), _( "Import" ) );
        LoadOneSymbol();
        SetToolID( ID_NO_TOOL_SELECTED, DrawPanel->GetDefaultCursor(), wxEmptyString );
        break;

    case ID_LIBEDIT_EXPORT_BODY_BUTT:
        SetToolID( id, DrawPanel->GetDefaultCursor(), _( "Export" ) );
        SaveOneSymbol();
        SetToolID( ID_NO_TOOL_SELECTED, DrawPanel->GetDefaultCursor(), wxEmptyString );
        break;

    case ID_LIBEDIT_DELETE_ITEM_BUTT:
        if( m_component == NULL )
        {
            wxBell();
            break;
        }

        SetToolID( id, wxCURSOR_BULLSEYE, _( "Delete item" ) );
        break;

    default:
        break;
    }

    DrawPanel->m_IgnoreMouseEvents = false;
}


void LIB_EDIT_FRAME::OnRotateItem( wxCommandEvent& aEvent )
{
    if( m_drawItem == NULL )
        return;

    if( !m_drawItem->InEditMode() )
    {
        SaveCopyInUndoList( m_component );
        m_drawItem->SetUnit( m_unit );
    }

    m_drawItem->Rotate();
	OnModify();

    if( !m_drawItem->InEditMode() )
        m_drawItem->ClearFlags();

    DrawPanel->Refresh();

    if( GetToolId() == ID_NO_TOOL_SELECTED )
        m_lastDrawItem = NULL;
}


LIB_ITEM* LIB_EDIT_FRAME::LocateItemUsingCursor( const wxPoint& aPosition,
                                                 const KICAD_T aFilterList[] )
{
    if( m_component == NULL )
        return NULL;

    LIB_ITEM* item = locateItem( aPosition, aFilterList );

    if( item == NULL )
        return NULL;

    wxPoint pos = GetScreen()->GetNearestGridPosition( aPosition );

    if( item == NULL && aPosition != pos )
        item = locateItem( pos, aFilterList );

    return item;
}


LIB_ITEM* LIB_EDIT_FRAME::locateItem( const wxPoint& aPosition, const KICAD_T aFilterList[] )
{
    if( m_component == NULL )
        return NULL;

    LIB_ITEM* item = NULL;

    m_collectedItems.Collect( m_component->GetDrawItemList(), aFilterList, aPosition,
                              m_unit, m_convert );

    if( m_collectedItems.GetCount() == 0 )
    {
        ClearMsgPanel();
    }
    else if( m_collectedItems.GetCount() == 1 )
    {
        item = m_collectedItems[0];
    }
    else
    {
        if( item == NULL )
        {
            wxASSERT_MSG( m_collectedItems.GetCount() <= MAX_SELECT_ITEM_IDS,
                          wxT( "Select item clarification context menu size limit exceeded." ) );

            wxMenu selectMenu;
            wxMenuItem* title = new wxMenuItem( &selectMenu, wxID_NONE, _( "Clarify Selection" ) );

            selectMenu.Append( title );
            selectMenu.AppendSeparator();

            for( int i = 0;  i < m_collectedItems.GetCount() && i < MAX_SELECT_ITEM_IDS;  i++ )
            {
                wxString text = m_collectedItems[i]->GetSelectMenuText();
                const char** xpm = m_collectedItems[i]->GetMenuImage();
                ADD_MENUITEM( &selectMenu, ID_SELECT_ITEM_START + i, text, xpm );
            }

            // Set to NULL in case user aborts the clarification context menu.
            m_drawItem = NULL;
            DrawPanel->m_AbortRequest = true;   // Changed to false if an item is selected
            PopupMenu( &selectMenu );
            DrawPanel->MoveCursorToCrossHair();
            item = m_drawItem;
        }
    }

    if( item )
        item->DisplayInfo( this );
    else
        ClearMsgPanel();

    return item;
}


void LIB_EDIT_FRAME::deleteItem( wxDC* aDC )
{
    wxCHECK_RET( m_drawItem != NULL, wxT( "No drawing item selected to delete." ) );

    DrawPanel->CrossHairOff( aDC );
    SaveCopyInUndoList( m_component );

    if( m_drawItem->Type() == LIB_PIN_T )
    {
        LIB_PIN* pin = (LIB_PIN*) m_drawItem;
        wxPoint pos = pin->GetPosition();

        m_component->RemoveDrawItem( (LIB_ITEM*) pin, DrawPanel, aDC );

        if( g_EditPinByPinIsOn == false )
        {
            LIB_PIN* tmp = m_component->GetNextPin();

            while( tmp != NULL )
            {
                pin = tmp;
                tmp = m_component->GetNextPin( pin );

                if( pin->GetPosition() != pos )
                    continue;

                m_component->RemoveDrawItem( (LIB_ITEM*) pin );
            }
        }
    }
    else
    {
        if( DrawPanel->IsMouseCaptured() )
            DrawPanel->m_endMouseCaptureCallback( DrawPanel, aDC );
        else
            m_component->RemoveDrawItem( m_drawItem, DrawPanel, aDC );
    }

    m_drawItem = NULL;
    OnModify();
    DrawPanel->CrossHairOn( aDC );
}


void LIB_EDIT_FRAME::OnSelectItem( wxCommandEvent& aEvent )
{
    int id = aEvent.GetId();
    int index = id - ID_SELECT_ITEM_START;

    if( (id >= ID_SELECT_ITEM_START && id <= ID_SELECT_ITEM_END)
        && (index >= 0 && index < m_collectedItems.GetCount()) )
    {
        LIB_ITEM* item = m_collectedItems[index];
        DrawPanel->m_AbortRequest = false;
        m_drawItem = item;
    }
}
