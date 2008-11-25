#ifndef SPRINGLOBBY_HEADERGUARD_NICKLISTCTRL_H
#define SPRINGLOBBY_HEADERGUARD_NICKLISTCTRL_H

#include "customvirtlistctrl.h"
#include "usermenu.h"

class User;
class UserList;
class Ui;
class ChatPanel;
class UserMenu;
class wxMutex;

class NickListCtrl : public CustomVirtListCtrl
{
  protected:
    typedef SL_GENERIC::UserMenu<ChatPanel> UserMenu;

  public:
    NickListCtrl( wxWindow* parent, bool show_header = true, UserMenu* popup = 0,
        bool singleSelectList = true, const wxString& name = _T("NickListCtrl"), bool highlight = true  );
    ~NickListCtrl();

    virtual void AddUser( const User& user );
 //   void AddUser( const UserList& userlist );
    void RemoveUser( const User& user );

    void UserUpdated( const User& user );

    int GetUserIndex( const User& user ) const;

    void GetSelectedUsers( UserList& users ) ;
    wxArrayString GetSelectedUserNicks( ) ;

    void ClearUsers();

    void OnActivateItem( wxListEvent& event );
    void OnColClick( wxListEvent& event );
    void OnShowMenu( wxContextMenuEvent& event );
    virtual void SetTipWindowText( const long item_hit, const wxPoint position);

    void HighlightItem( long item );

    void SortList();

    //these are overloaded to use list in virtual style
    virtual wxString OnGetItemText(long item, long column) const;
    virtual int OnGetItemImage(long item) const;
    virtual int OnGetItemColumnImage(long item, long column) const;

  protected:
    virtual void Sort();

    typedef std::vector< const User* > DataVec;
    typedef DataVec::iterator DataIter;
    typedef DataVec::const_iterator DataCIter;
    DataVec m_data;

    typedef bool  (*CompareFunc)  ( const User*, const User*,  SortOrder&  )  ;

    CompareFunc m_compare_func;

    UserMenu* m_menu;

    enum {
      NICK_LIST = 31765 //wxID_HIGHEST
      //wxID_HIGHEST is used by BattleListCTRL. The cant be in the same Tab like BattleTab
    };


    DECLARE_EVENT_TABLE()
};


#endif // SPRINGLOBBY_HEADERGUARD_NICKLISTCTRL_H
