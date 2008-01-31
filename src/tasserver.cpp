/* Copyright (C) 2007 The SpringLobby Team. All rights reserved. */

#include <wx/string.h>
#include <wx/regex.h>
#include <wx/intl.h>
#include <stdexcept>

#include "base64.h"
#include "boost/md5.hpp"
#include "tasserver.h"
#include "iunitsync.h"
#include "user.h"
#include "utils.h"
#include "battle.h"
#include "serverevents.h"
#include "socket.h"

//#include <wx/socket.h>

//! @brief Struct used internally by the TASServer class to get client status information.
struct TASClientstatus {
  unsigned int in_game   : 1;
  unsigned int away      : 1;
  unsigned int rank      : 3;
  unsigned int moderator : 1;
  unsigned int bot       : 1;
};


//! @brief Union used internally by the TASServer class to get client status information.
union UTASClientStatus {
  unsigned char byte;
  TASClientstatus tasdata;
};


//! @brief Struct used internally by the TASServer class to get battle status information.
struct TASBattleStatus {
  unsigned int : 1;
  unsigned int ready : 1;
  unsigned int team : 4;
  unsigned int ally : 4;
  unsigned int player : 1;
  unsigned int handicap: 7;
  unsigned int : 4;
  unsigned int sync : 2;
  unsigned int side : 4;
  unsigned int : 4;
};


//! @brief Union used internally by the TASServer class to get battle status information.
union UTASBattleStatus {
  int data;
  TASBattleStatus tasdata;
};


struct TASColor {
  unsigned int red : 8;
  unsigned int green : 8;
  unsigned int blue : 8;
  unsigned int zero: 8;
};


union UTASColor {
  int data;
  TASColor color;
};


/*

myteamcolor:  Should be 32-bit signed integer in decimal form (e.g. 255 and not FF) where each color channel should occupy 1 byte (e.g. in hexdecimal: $00BBGGRR, B = blue, G = green, R = red). Example: 255 stands for $000000FF.

*/

UserStatus ConvTasclientstatus( TASClientstatus );
UserBattleStatus ConvTasbattlestatus( TASBattleStatus );
TASBattleStatus ConvTasbattlestatus( UserBattleStatus );
StartType IntToStartType( int start );
NatType IntToNatType( int nat );
GameType IntToGameType( int gt );

TASServer::TASServer( Ui& ui ): Server(ui), m_ui(ui), m_ser_ver(0), m_connected(false), m_online(false), m_buffer(_T("")), m_last_ping(0), m_ping_id(1000),m_battle_id(-1) { m_se = new ServerEvents( *this, ui); }

TASServer::~TASServer() { delete m_se; }


bool TASServer::ExecuteSayCommand( const wxString& cmd )
{
  if ( m_sock == 0 ) return false;
  wxString subcmd = cmd.BeforeFirst(' ').Lower();
  wxString params = cmd.AfterFirst( ' ' );
  if ( subcmd == _T("/ingame") ) {
    SendCmd( _T("GETINGAMETIME"), params );
    return true;
  } else if ( subcmd == _("/kick") ) {
    SendCmd( _T("KICKUSER"), params );
    return true;
  } else if ( subcmd == _("/ban") ) {
    SendCmd( _T("BANLISTADD"), params );
    return true;
  } else if ( subcmd == _T("/unban") ) {
    SendCmd( _T("BANLISTREMOVE"), params );
    return true;
  } else if ( subcmd == _T("/banlist") ) {
    SendCmd( _T("BANLIST") );
    return true;
  } else if ( subcmd == _T("/topic") ) {
    SendCmd( _T("CHANNELTOPIC"), params );
    return true;
  } else if ( subcmd == _T("/chanmsg") ) {
    SendCmd( _T("CHANNELMESSAGE"), params );
    return true;
  } else if ( subcmd == _T("/ring") ) {
    SendCmd( _T("RING"), params );
    return true;
  } else if ( subcmd == _T("/ip") ) {
    SendCmd( _T("GETIP"), params );
    return true;
  } else if ( subcmd == _T("/mute") ) {
    SendCmd( _T("MUTE"), params );
    return true;
  } else if ( subcmd == _T("/unmute") ) {
    SendCmd( _T("UNMUTE"), params );
    return true;
  } else if ( subcmd == _T("/mutelist") ) {
    SendCmd( _T("MUTELIST"), params );
    return true;
  } else if ( subcmd == _T("/lastlogin") ) {
    SendCmd( _T("GETLASTLOGINTIME"), params );
    return true;
  } else if ( subcmd == _T("/findip") ) {
    SendCmd( _T("FINDIP"), params );
    return true;
  } else if ( subcmd == _T("/lastip") ) {
    SendCmd( _T("GETLASTIP"), params );
    return true;
  } else if ( subcmd == _T("/rename") ) {
    SendCmd( _T("RENAMEACCOUNT"), params );
    return true;
  } else if ( subcmd == _T("/testmd5") ) {
    ExecuteCommand( _T("SERVERMSG"), GetPasswordHash(params) );
    return true;
  }

  return false;
}


void TASServer::SetSocket( Socket* sock )
{
  Server::SetSocket( sock );
  if ( m_sock == 0 ) return;
  m_sock->SetSendRateLimit( 800 ); // 1250 is the server limit but 800 just to make sure :)
}


void TASServer::Connect( const wxString& addr, const int port )
{
  m_addr=addr;
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  m_sock->Connect( addr, port );
  if ( IsConnected() ) {
    m_last_ping = time( 0 );
    m_connected = true;
  }
  m_sock->SetPingInfo( _T("PING\n"), 10000 );
  m_online = false;
  m_agreement = _T("");
}

void TASServer::Disconnect()
{
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );
  SendCmd( _T("EXIT") ); // EXIT command for new protocol compatibility
  m_sock->Disconnect();
  m_connected = false;
}

bool TASServer::IsConnected()
{
  if ( m_sock == 0 ) return false;
  return (m_sock->State() == SS_OPEN);
}


bool TASServer::Register( const wxString& addr, const int port, const wxString& nick, const wxString& password, wxString* reason )
{
  wxLogDebugFunc( _T("") );

  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );
  m_sock->Connect( addr, port );
  if ( !IsConnected() ) return false;

  wxString data;
  m_sock->Receive( data );
  if ( GetWordParam( data ) != _T("TASServer") ) return false;

  SendCmd ( _T("REGISTER "), nick + _T(" ") + GetPasswordHash( password ) );

  wxString data2;
  m_sock->Receive( data2 );
  if ( data2 != _T("REGISTRATIONACCEPTED\n"))
  {
	  *reason = data2.substr(19,data2.size());
	  return false;
  }

  return true;
}


bool TASServer::IsPasswordHash( const wxString& pass )
{
  return pass.length() == 24 && pass[22] == '=' && pass[23] == '=';
}


wxString TASServer::GetPasswordHash( const wxString& pass )
{
  if ( IsPasswordHash(pass) ) return pass;
  return WX_STRING(base64_encode(boost::md5(pass.mb_str()).digest().value(), 16));
}


User& TASServer::GetMe()
{
  return GetUser( m_user );
}


void TASServer::Login()
{
  wxLogDebugFunc( _T("") );

  SendCmd ( _T("LOGIN"), m_user + _T(" ") + GetPasswordHash( m_pass ) + _T(" 2100 * SpringLobby 0.1") );

}

void TASServer::Logout()
{
  wxLogDebugFunc( _T("") );
  Disconnect();
}

bool TASServer::IsOnline() {
  if ( !m_connected ) return false;
  return m_online;
}


void TASServer::RequestChannels()
{
  SendCmd( _T("CHANNELS") );
}


void TASServer::AcceptAgreement()
{
  SendCmd( _T("CONFIRMAGREEMENT") );
}


void TASServer::Update( int mselapsed )
{
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  m_sock->OnTimer( mselapsed );

  if ( !m_connected ) { // We are not formally connected yet, but might be.
    if ( IsConnected() ) {
      m_last_ping = time( 0 );
      m_connected = true;
    }
    return;
  } else { // We are connected already.
    if ( !IsConnected() ) {
      m_connected = false;
      m_online = false;
      m_se->OnDisconnected();
      return;
    }

    /*time_t now = time( 0 );
    if ( m_last_ping + m_keepalive < now ) { // Is it time for a keepalive PING?
      Ping();
      /// Nat travelsal "ping"
      Battle *battle=GetCurrentBattle();
      if(battle){
        if((battle->GetNatType()==NAT_Hole_punching || (battle->GetNatType()==NAT_Fixed_source_ports)) && !battle->GetInGame()){
          UDPPing();
        }else{
          /// old logging for debug
          //if(battle->GetNatType()!=NAT_Hole_punching)wxLogMessage( _T("pinging: current battle not using NAT_Hole_punching") );
          //if(battle->GetInGame())wxLogMessage( _T("pinging: current battle is in game") );
        }
      }else{
        //wxLogMessage( _T("pinging: No current battle set") );
      }
    }*/
    HandlePinglist();
  }

  ReceiveAndExecute();

}


void TASServer::ReceiveAndExecute()
{
  wxString data;

  do {

    data = _T("");
    if ( m_sock->Receive( data ) ) {
      m_buffer += data;
      if ( m_buffer.Find( _T("\n") ) != -1 ) {
        wxString cmd = m_buffer.substr( 0, m_buffer.find( _T("\n"), 0 ) );
        m_buffer = m_buffer.substr( m_buffer.find( _T("\n"), 0 )+1, m_buffer.length() );
        ExecuteCommand( cmd );
      }
    }

  } while ( !data.empty() ); // Go on until recive stops providing data.
}


wxString ConvertTASServerPhailChecksum( const wxString& buggedcsum )
{
  signed long temp;
  buggedcsum.ToLong( &temp );
  unsigned int temp2 = (unsigned int)temp;
  return wxString::Format( _T("%u"), temp2 );
}


wxString ConvertToTASServerBuggedChecksum( const wxString& csum )
{
  unsigned long temp;
  csum.ToULong( &temp );
  int temp2 = (int)temp;
  return wxString::Format( _T("%d"), temp2 );
}


void TASServer::ExecuteCommand( const wxString& in )
{
  wxLogMessage( _T("%s"), in.c_str()  );
  wxString cmd;
  wxString params = in;
  long replyid;

  if ( in.empty() ) return;

  ASSERT_LOGIC( params.BeforeLast( '\n' ).IsEmpty(), _T("losing data") );
  cmd = params.BeforeFirst( ' ' );
   if ( params[0] == '#' )
  {
    params = params.AfterFirst( ' ' );
    params.ToLong( &replyid );
  }
  else
    params = params.AfterFirst( ' ' );

  ExecuteCommand( cmd, params, replyid );
}


void TASServer::ExecuteCommand( const wxString& cmd, const wxString& inparams, int replyid )
{
  wxLogMessage( _T("'%s' '%s'"), cmd.c_str(), inparams.c_str() );
  wxString params = inparams;
  int pos, cpu, id, nat, port, maxplayers, rank, specs, units, top, left, right, bottom, ally, udpport;
  bool replay, haspass,lanmode = false;
  wxString hash;
  wxString nick, contry, host, map, title, mod, channel, error, msg, owner, ai, supported_spring_version;
  //NatType ntype;
  UserStatus cstatus;
  UTASClientStatus tasstatus;
  UTASBattleStatus tasbstatus;
  UserBattleStatus bstatus;
  UTASColor color;

  if ( cmd == _T("TASServer")) {
    mod = GetWordParam( params );
    mod.ToDouble( &m_ser_ver );
    supported_spring_version = GetWordParam( params );
    udpport = GetIntParam( params );
    lanmode = GetBoolParam( params );
    m_se->OnConnected( _T("TAS Server"), mod, (m_ser_ver > 0), supported_spring_version, udpport, lanmode );

  } else if ( cmd == _T("ACCEPTED") ) {
    m_online = true;
    m_se->OnLogin( );
  } else if ( cmd == _T("MOTD") ) {
    m_se->OnMotd( params );
  } else if ( cmd == _T("ADDUSER") ) {
    nick = GetWordParam( params );
    contry = GetWordParam( params );
    cpu = GetIntParam( params );
    m_se->OnNewUser( nick, contry, cpu );
  } else if ( cmd == _T("CLIENTSTATUS") ) {
    nick = GetWordParam( params );
    tasstatus.byte = GetIntParam( params );
    cstatus = ConvTasclientstatus( tasstatus.tasdata );
    m_se->OnUserStatus( nick, cstatus );
  } else if ( cmd == _T("BATTLEOPENED") ) {
    id = GetIntParam( params );
    replay = GetBoolParam( params );
    nat = GetIntParam( params );
    nick = GetWordParam( params );
    host = GetWordParam( params );
    port = GetIntParam( params );
    maxplayers = GetIntParam( params );
    haspass = GetBoolParam( params );
    rank = GetIntParam( params );
    hash = ConvertTASServerPhailChecksum( GetWordParam( params ) );
    map = GetSentenceParam( params );
    title = GetSentenceParam( params );
    mod = GetSentenceParam( params );
    m_se->OnBattleOpened( id, replay, IntToNatType( nat ), nick, host, port, maxplayers,
                            haspass, (rank + 1)*100, hash, map, title, mod );
  } else if ( cmd == _T("JOINEDBATTLE") ) {
    id = GetIntParam( params );
    nick = GetWordParam( params );
    m_se->OnUserJoinedBattle( id, nick );
  } else if ( cmd == _T("UPDATEBATTLEINFO") ) {
    id = GetIntParam( params );
    specs = GetIntParam( params );
    haspass = GetBoolParam( params );
    hash = ConvertTASServerPhailChecksum( GetWordParam( params ) );
    map = GetSentenceParam( params );
    m_se->OnBattleInfoUpdated( id, specs, haspass, hash, map );
  } else if ( cmd == _T("LOGININFOEND") ) {
    m_se->OnLoginInfoComplete();
  } else if ( cmd == _T("REMOVEUSER") ) {
    nick = GetWordParam( params );
    m_se->OnUserQuit( nick );
  } else if ( cmd == _T("BATTLECLOSED") ) {
    id = GetIntParam( params );
    m_se->OnBattleClosed( id );
  } else if ( cmd == _T("LEFTBATTLE") ) {
    id = GetIntParam( params );
    nick = GetWordParam( params );
    m_se->OnUserLeftBattle( id, nick );
  } else if ( cmd == _T("PONG") ) {
    HandlePong( replyid );
  } else if ( cmd == _T("JOIN") ) {
    channel = GetWordParam( params );
    m_se->OnJoinChannelResult( true, channel, _T("") );
  } else if ( cmd == _T("JOIN") ) {
    channel = GetWordParam( params );
    error = GetSentenceParam( params );
    m_se->OnJoinChannelResult( false, channel, error );
  } else if ( cmd == _T("SAID") ) {
    channel = GetWordParam( params );
    nick = GetWordParam( params );
    m_se->OnChannelSaid( channel, nick, params );
  } else if ( cmd == _T("JOINED") ) {
    channel = GetWordParam( params );
    nick = GetWordParam( params );
    m_se->OnUserJoinChannel( channel, nick );
  } else if ( cmd == _T("LEFT") ) {
    channel = GetWordParam( params );
    nick = GetWordParam( params );
    msg = GetSentenceParam( params );
    m_se->OnChannelPart( channel, nick, msg );
  } else if ( cmd == _T("CHANNELTOPIC") ) {
    channel = GetWordParam( params );
    nick = GetWordParam( params );
    pos = GetIntParam( params );
    m_se->OnChannelTopic( channel, nick, params, pos/1000 );
  } else if ( cmd == _T("SAIDEX") ) {
    channel = GetWordParam( params );
    nick = GetWordParam( params );
    m_se->OnChannelAction( channel, params, msg );
  } else if ( cmd == _T("CLIENTS") ) {
    channel = GetWordParam( params );
    while ( (nick = GetWordParam( params )) != _T("") ) {
      m_se->OnChannelJoin( channel, nick );
    }
  } else if ( cmd == _T("SAYPRIVATE") ) {
    nick = GetWordParam( params );
    m_se->OnPrivateMessage( nick, params, true );
  } else if ( cmd == _T("SAIDPRIVATE") ) {
    nick = GetWordParam( params );
    m_se->OnPrivateMessage( nick, params, false );
  } else if ( cmd == _T("JOINBATTLE") ) {
    id = GetIntParam( params );
    hash = ConvertTASServerPhailChecksum( GetWordParam( params ) );
    m_battle_id = id;
    m_se->OnJoinedBattle( id );
    m_se->OnBattleInfoUpdated( m_battle_id );
  } else if ( cmd == _T("CLIENTBATTLESTATUS") ) {
    nick = GetWordParam( params );
    tasbstatus.data = GetIntParam( params );
    bstatus = ConvTasbattlestatus( tasbstatus.tasdata );
    color.data = GetIntParam( params );
    bstatus.color_r = color.color.red;
    bstatus.color_g = color.color.green;
    bstatus.color_b = color.color.blue;
    m_se->OnClientBattleStatus( m_battle_id, nick, bstatus );
  } else if ( cmd == _T("ADDSTARTRECT") ) {
    //ADDSTARTRECT allyno left top right bottom
    ally = GetIntParam( params );
    left = GetIntParam( params );
    top = GetIntParam( params );
    right = GetIntParam( params );
    bottom = GetIntParam( params );;
    m_se->OnBattleStartRectAdd( m_battle_id, ally, left, top, right, bottom );
  } else if ( cmd == _T("REMOVESTARTRECT") ) {
    //REMOVESTARTRECT allyno
    ally = GetIntParam( params );
    m_se->OnBattleStartRectRemove( m_battle_id, ally );
  } else if ( cmd == _T("ENABLEALLUNITS") ) {
    //"ENABLEALLUNITS" params: "".
    m_se->OnBattleEnableAllUnits( m_battle_id );
  } else if ( cmd == _T("ENABLEUNITS") ) {
    //ENABLEUNITS unitname1 unitname2
    while ( (nick = GetWordParam( params )) !=_T( "") ) {
      m_se->OnBattleEnableUnit( m_battle_id, nick );
    }
  } else if ( cmd == _T("DISABLEUNITS") ) {
    //"DISABLEUNITS" params: "arm_advanced_radar_tower arm_advanced_sonar_station arm_advanced_torpedo_launcher arm_dragons_teeth arm_energy_storage arm_eraser arm_fark arm_fart_mine arm_fibber arm_geothermal_powerplant arm_guardian"
    while ( (nick = GetWordParam( params )) != _T("") ) {
      m_se->OnBattleDisableUnit( m_battle_id, nick );
    }
  } else if ( cmd == _T("CHANNEL") ) {
    channel = GetWordParam( params );
    units = GetIntParam( params );
    m_se->OnChannelList( channel, units );
  } else if ( cmd == _T("ENDOFCHANNELS") ) {
    //Cmd: ENDOFCHANNELS params:
  } else if ( cmd == _T("REQUESTBATTLESTATUS") ) {
    m_se->OnRequestBattleStatus( m_battle_id );
  } else if ( cmd == _T("SAIDBATTLE") ) {
    nick = GetWordParam( params );
    m_se->OnSaidBattle( m_battle_id, nick, params );
  } else if ( cmd == _T("SAIDBATTLEEX") ) {
    nick = GetWordParam( params );
    m_se->OnBattleAction( m_battle_id, nick, params );
  } else if ( cmd == _T("AGREEMENT") ) {
    msg = GetSentenceParam( params );
    m_agreement += msg + _T("\n");
  } else if ( cmd == _T("AGREEMENTEND") ) {
    m_se->OnAcceptAgreement( m_agreement );
    m_agreement = _T("");
  } else if ( cmd == _T("OPENBATTLE") ) {
    m_battle_id = GetIntParam( params );
    m_se->OnHostedBattle( m_battle_id );
    SendHostInfo( HI_Send_All_opts );
  } else if ( cmd == _T("ADDBOT") ) {
    // ADDBOT BATTLE_ID name owner battlestatus teamcolor {AIDLL}
    id = GetIntParam( params );
    nick = GetWordParam( params );
    owner = GetWordParam( params );
    tasbstatus.data = GetIntParam( params );
    bstatus = ConvTasbattlestatus( tasbstatus.tasdata );
    color.data = GetIntParam( params );
    bstatus.color_r = color.color.red;
    bstatus.color_g = color.color.green;
    bstatus.color_b = color.color.blue;
    ai = GetSentenceParam( params );
    ai = ai.BeforeLast( '.' );
    m_se->OnBattleAddBot( id, nick, owner, bstatus, ai );
  } else if ( cmd == _T("UPDATEBOT") ) {
    id = GetIntParam( params );
    nick = GetWordParam( params );
    tasbstatus.data = GetIntParam( params );
    bstatus = ConvTasbattlestatus( tasbstatus.tasdata );
    color.data = GetIntParam( params );
    bstatus.color_r = color.color.red;
    bstatus.color_g = color.color.green;
    bstatus.color_b = color.color.blue;
    m_se->OnBattleUpdateBot( id, nick, bstatus );
    //UPDATEBOT BATTLE_ID name battlestatus teamcolor
  } else if ( cmd == _T("REMOVEBOT") ) {
    id = GetIntParam( params );
    nick = GetWordParam( params );
    m_se->OnBattleRemoveBot( id, nick );
    //REMOVEBOT BATTLE_ID name
  } else if ( cmd == _T("RING") ) {
    nick = GetWordParam( params );
    m_se->OnRing( nick );
    //RING username
  } else if ( cmd == _T("SERVERMSG") ) {
    // if a moderator asked for a players IP, just give him all smurfs
    wxRegEx ipregex( _T("'s IP is (([0-9]{1,3}\\.){3}[0-9]{1,3})") );
    if ( ipregex.Matches( params ) ) {
      ModeratorFindByIP(  ipregex.GetMatch( params, 1 ) );
    } else {
      m_se->OnServerMessage( params );
    }
    //SERVERMSG {message}
  } else if ( cmd == _T("JOINBATTLEFAILED") ) {
    msg = GetSentenceParam( params );
    m_se->OnServerMessage( _T("Failed to join battle. ") + msg );
    //JOINBATTLEFAILED {reason}
  } else if ( cmd == _T("OPENBATTLEFAILED") ) {
    msg = GetSentenceParam( params );
    m_se->OnServerMessage( _T("Failed to host new battle on server. ") + msg );
    //OPENBATTLEFAILED {reason}
  } else if ( cmd == _T("JOINFAILED") ) {
    channel = GetWordParam( params );
    msg = GetSentenceParam( params );
    m_se->OnServerMessage( _T("Failed to join channel #") + channel + _T(". ") + msg );
    //JOINFAILED channame {reason}
  } else if ( cmd == _T("CHANNELMESSAGE") ) {
    channel = GetWordParam( params );
    m_se->OnChannelMessage( channel, params );
    //CHANNELMESSAGE channame {message}
  } else if ( cmd == _T("FORCELEAVECHANNEL") ) {
    channel = GetWordParam( params );
    nick = GetWordParam( params );
    msg = GetSentenceParam( params );
    m_se->OnChannelPart( channel, GetMe().GetNick(), _T("Kicked by <") + nick + _T("> ") + msg );
    //FORCELEAVECHANNEL channame username [{reason}]
  } else if ( cmd == _T("DENIED") ) {
    msg = GetSentenceParam( params );
    m_se->OnServerMessage( msg );
    //Command: "DENIED" params: "Already logged in".
  } else if ( cmd == _T("HOSTPORT") ) {
    int tmp_port = GetIntParam( params );
    m_se->OnHostUdpPortChange( tmp_port );
    //HOSTPORT port
  } else if ( cmd == _T("UDPSOURCEPORT") ) {
    int tmp_port = GetIntParam( params );
    m_se->OnUdpSourcePort( tmp_port );
    //HOSTPORT port
  } else if ( cmd == _T("SETSCRIPTTAGS") ) {
    wxString command;
    while ( (command = GetSentenceParam( params )) != _T("") ) {
      wxString key = command.BeforeFirst( '=' );
      wxString value = command.AfterFirst( '=' );
      m_se->OnSetBattleInfo( m_battle_id, key, value );
    }
    m_se->OnBattleInfoUpdated( m_battle_id );
    // !! Command: "SETSCRIPTTAGS" params: "game/startpostype=0	game/maxunits=1000	game/limitdgun=0	game/startmetal=1000	game/gamemode=0	game/ghostedbuildings=-1	game/startenergy=1000	game/diminishingmms=0"
  } else if ( cmd == _T("FORCEQUITBATTLE")) {
	  m_se->OnKickedFromBattle();
  } else {
    wxLogMessage( _T("??? Cmd: %s params: %s"), cmd.c_str(), params.c_str() );
    m_se->OnUnknownCommand( cmd, params );
  }
}


void TASServer::SendCmd( const wxString& command, const wxString& param )
{
  if ( param.IsEmpty() ) m_sock->Send( command + _T("\n"));
  else m_sock->Send( command + _T(" ") + param + _T("\n") );
}


void TASServer::Ping()
{
  //wxLogDebugFunc( _T("") );
  wxString cmd = _T("");

  m_ping_id++;
  if (  m_ser_ver > 0 )
    SendCmd( _T("PING") );
  else
    SendCmd( _T("PING"), wxString::Format( _T("%d"), m_ping_id) );

  TASPingListItem pli;
  pli.id = m_ping_id;
  pli.t = time( 0 );
  m_pinglist.push_back ( pli );
  m_last_ping = time( 0 );
}


void TASServer::HandlePong( int replyid )
{
  std::list<TASPingListItem>::iterator it;

  bool found = false;
  for ( it = m_pinglist.begin(); it != m_pinglist.end(); it++ ) {
    if (it->id == replyid ) {
      found = true;
      break;
    }
  }

  if ( found ) {
    m_se->OnPong( (time( 0 ) - it->t) );
    m_pinglist.erase( it );
  } else {
    if ( !m_pinglist.empty() ) {
      m_se->OnPong( (time( 0 ) - m_pinglist.begin()->t) );
      m_pinglist.pop_front();
    } else {
      m_se->OnPong( -2 );
    }
  }
}


void TASServer::HandlePinglist()
{
  std::list<TASPingListItem>::iterator it;
  int now = time( 0 );
  while ( !m_pinglist.empty() ) {
    if ( m_pinglist.begin()->t + PING_TIMEOUT < now ) {
      m_pinglist.pop_front();
      m_se->OnPong( -1 );
    } else {
      break;
    }
  }
}


void TASServer::JoinChannel( const wxString& channel, const wxString& key )
{
  //JOIN channame [key]
  wxLogDebugFunc( channel );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  SendCmd ( _T("JOIN"), key );
}


void TASServer::PartChannel( const wxString& channel )
{
  //LEAVE channame
  wxLogDebugFunc( channel );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  SendCmd( _T("LEAVE"), channel );

}


void TASServer::DoActionChannel( const wxString& channel, const wxString& msg )
{
  //SAYEX channame {message}
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  SendCmd( _T("SAYEX"), channel + _T(" ") + msg );
}


void TASServer::SayChannel( const wxString& channel, const wxString& msg )
{
  //SAY channame {message}
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  SendCmd( _T("SAY"), channel + _T(" ") + msg );
}


void TASServer::SayPrivate( const wxString& nick, const wxString& msg )
{
  //SAYPRIVATE username {message}
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  SendCmd( _T("SAYPRIVATE"), nick + _T(" ") + msg );
}


void TASServer::DoActionPrivate( const wxString& nick, const wxString& msg )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  SendCmd( _T("SAYPRIVATEEX"), nick + _T(" ") + msg );
}


void TASServer::SayBattle( int battleid, const wxString& msg )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  SendCmd( _T("SAYBATTLE"), msg );
}


void TASServer::DoActionBattle( int battleid, const wxString& msg )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  SendCmd( _T("SAYBATTLEEX"), msg );
}


void TASServer::Ring( const wxString& nick )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  SendCmd( _T("RING"), nick );
}




void TASServer::ModeratorSetChannelTopic( const wxString& channel, const wxString& topic )
{
  SendCmd( _T("CHANNELTOPIC"), channel + _T(" ") + topic );
}


void TASServer::ModeratorSetChannelKey( const wxString& channel, const wxString& key)
{
  SendCmd( _T("SETCHANNELKEY"), channel + _T(" ") + key );
}


void TASServer::ModeratorMute( const wxString& channel, const wxString& nick, int duration, bool byip )
{
  SendCmd( _T("MUTE"), channel + _T(" ") + nick + _T(" ") + wxString::Format( _T("%d"), duration) + (byip?_T(" ip"):_T("") )  );
}


void TASServer::ModeratorUnmute( const wxString& channel, const wxString& nick )
{
  SendCmd( _T("UNMUTE"),  channel + _T(" ") + nick );
}


void TASServer::ModeratorKick( const wxString& channel, const wxString& reason )
{
  SendCmd( _T("KICKUSER"), channel + _T(" ") + reason );
}


void TASServer::ModeratorBan( const wxString& nick, bool byip )
{
  // FIXME TASServer::ModeratorBan not yet implemented
}


void TASServer::ModeratorUnban( const wxString& nick )
{
  // FIXME TASServer::ModeratorUnban not yet implemented
}


void TASServer::ModeratorGetIP( const wxString& nick )
{
  SendCmd( _T("GETIP"), nick );
}


void TASServer::ModeratorGetLastLogin( const wxString& nick )
{
  SendCmd( _T("GETLASTLOGINTIME"), nick );
}


void TASServer::ModeratorGetLastIP( const wxString& nick )
{
  SendCmd( _T("GETLASTIP"), nick );
}


void TASServer::ModeratorFindByIP( const wxString& ipadress )
{
  SendCmd( _T("FINDIP"), ipadress );
}


void TASServer::AdminGetAccountAccess( const wxString& nick )
{
  // FIXME TASServer::AdminGetAccountAccess not yet implemented
}


void TASServer::AdminChangeAccountAccess( const wxString& nick, const wxString& accesscode )
{
  // FIXME TASServer::AdminChangeAccountAccess not yet implemented
}


void TASServer::AdminSetBotMode( const wxString& nick, bool isbot )
{
  SendCmd( _T("SETBOTMODE"), nick + _T(" ") + (isbot?_T("1"):_T("0")) );
}





void TASServer::HostBattle( BattleOptions bo, const wxString& password )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  wxString cmd = wxString::Format( _T("0 %d "), bo.nattype );
  cmd += (password.IsEmpty())?_T("*"):password;
  cmd += wxString::Format( _T(" %d %d "),
    bo.port,
    bo.maxplayers
  );
  cmd += ConvertToTASServerBuggedChecksum ( bo.modhash );
  cmd += wxString::Format( _T(" %d "), bo.rankneeded/100 );
  cmd += ConvertToTASServerBuggedChecksum(  bo.maphash ) + _T(" ");
  cmd += bo.mapname + _T("\t");
  cmd += bo.description + _T("\t");
  cmd += bo.modname;
  //wxLogMessage( _T("%s"), cmd.c_str() );
  SendCmd( _T("OPENBATTLE"), cmd );

  // OPENBATTLE type natType password port maphash {map} {title} {modname}
}


void TASServer::JoinBattle( const int& battleid, const wxString& password )
{
  //JOINBATTLE BATTLE_ID [parameter]
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  //UDPPing();
  if(BattleExists(battleid)){
    Battle *battle=&GetBattle(battleid);
    if(battle){
      m_sock->SetUdpPingInfo( m_addr, m_udp_port, 10000 );
      //if((battle->GetNatType()==NAT_Hole_punching)||(battle->GetNatType()==NAT_Fixed_source_ports))UDPPing();
    }
  }else{
    wxLogMessage( _T("battle doesnt exist") );
  }

  SendCmd( _T("JOINBATTLE"), wxString::Format( _T("%d"), battleid ) + _T(" ") + password );
}


void TASServer::LeaveBattle( const int& battleid )
{
  //LEAVEBATTLE
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  m_sock->SetUdpPingInfo();
  SendCmd( _T("LEAVEBATTLE") );
}


void TASServer::SendHostInfo( HostInfo update )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  Battle& battle = GetBattle( m_battle_id );
  ASSERT_LOGIC( battle.IsFounderMe(), _T("I'm not founder") );

  //BattleOptions bo = battle.opts();

  if ( ( update & ( HI_Map | HI_Locked | HI_Spectators ) ) > 0 ) {
    // UPDATEBATTLEINFO SpectatorCount locked maphash {mapname}
    wxString cmd = wxString::Format( _T(" %d %d "), battle.GetSpectators(), battle.IsLocked() );
    cmd += ConvertToTASServerBuggedChecksum( battle.GetMapHash() ) + _T(" ");
    cmd += battle.GetMapName();

    SendCmd( _T("UPDATEBATTLEINFO"), cmd );
  }
  if ( ( update & HI_Send_All_opts ) > 0 ) {
    wxString cmd;

    wxStringTripleVec optlistMap;
    battle.CustomBattleOptions()->getOptions( &optlistMap, MapOption );
    for (wxStringTripleVec::iterator it = optlistMap.begin(); it != optlistMap.end(); ++it)
    {
      cmd += _T("game\\mapoptions\\") + it->first + _T("=") + it->second.second + _T("\t");
    }
    wxStringTripleVec optlistMod;
    battle.CustomBattleOptions()->getOptions( &optlistMod, ModOption );
    for (wxStringTripleVec::iterator it = optlistMod.begin(); it != optlistMod.end(); ++it)
    {
      cmd += _T("game\\modoptions\\") + it->first + _T("=") + it->second.second + _T("\t");
    }
/// FIXME (BrainDamage#1#): change the slash type when new sprring comes out
    wxStringTripleVec optlistEng;
    battle.CustomBattleOptions()->getOptions( &optlistEng, EngineOption );
    for (wxStringTripleVec::iterator it = optlistEng.begin(); it != optlistEng.end(); ++it)
    {
      cmd += _T("game/") + it->first + _T("=") + it->second.second + _T("\t");
    }

    SendCmd( _T("SETSCRIPTTAGS"), cmd );
  }

  if ( (update & HI_StartRects) > 0 ) { // Startrects should be updated.

    for ( std::vector<BattleStartRect*>::size_type i = 16; i < battle.GetNumRects(); i++ ) battle.RemoveStartRect( i ); /// FIXME (BrainDamage#1#):  remove this when not needing to connect to TASserver (because doesn't support >16 start boxes)
    for ( std::vector<BattleStartRect*>::size_type i = 0; i < battle.GetNumRects(); i++ ) { // Loop through all, and remove updated or deleted.
      if ( i >= 16 ) break; /// FIXME (BrainDamage#1#):  remove this when not needing to connect to TASserver (because doesn't support >16 start boxes)
      wxString cmd;
      BattleStartRect* sr = battle.GetStartRect( i );
      if ( sr == 0 ) continue;
      if ( sr->deleted ) {
        SendCmd( _T("REMOVESTARTRECT"), wxString::Format( _T("%d"), i ) );
        battle.StartRectRemoved( i );
      } else if ( !sr->local && sr->updated ) {
        cmd = wxString::Format( _T("REMOVESTARTRECT %d\n"), i );
        SendCmd( _T("REMOVESTARTRECT"), wxString::Format( _T("%d"), i ) );
      }

    }

    for ( std::vector<BattleStartRect*>::size_type i = 0; i < battle.GetNumRects(); i++ ) { // Loop through all, and update.
      if ( i >= 16 ) break; /// FIXME (BrainDamage#1#):  remove this when not needing to connect to TASserver (because doesn't support >16 start boxes)
      wxString cmd;
      BattleStartRect* sr = battle.GetStartRect( i );
      if ( sr == 0 ) continue;
      if ( sr->updated ) {
        SendCmd( _T("ADDSTARTRECT"), wxString::Format( _T("%d %d %d %d %d\n"), sr->ally, sr->left, sr->top, sr->right, sr->bottom ) );
        battle.StartRectUpdated( i );

      }
    }

  }
  if ( (update & HI_Restrictions) > 0 ) {
    wxArrayString units = battle.DisabledUnits();
    SendCmd( _T("ENABLEALLUNITS") );
    if ( units.GetCount() > 0 )
    {
      wxString msg;
      for ( unsigned int i = 0; i < units.GetCount(); i++ ) msg += units[i] + _T(" ");
      SendCmd( _T("DISABLEUNITS"), msg );
    }
  }
}


void TASServer::SendHostInfo( const wxString& Tag )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  Battle& battle = GetBattle( m_battle_id );
  ASSERT_LOGIC( battle.IsFounderMe(), _T("I'm not founder") );

  wxString cmd;

  long type;
  Tag.BeforeFirst( '_' ).ToLong( &type );
  wxString key = Tag.AfterFirst( '_' );
  if ( type == MapOption )
  {
    cmd += _T("game\\mapoptions\\") + key + _T("=") + battle.CustomBattleOptions()->getSingleValue( key, MapOption ) + _T("\n");
  }
  else if ( type == ModOption )
  {
    cmd += _T("game\\modoptions\\") + key + _T("=") + battle.CustomBattleOptions()->getSingleValue( key, ModOption ) + _T("\n");
  }
  else if ( type == EngineOption )
  {
    cmd += _T("game/") + key + _T("=") + battle.CustomBattleOptions()->getSingleValue( key, EngineOption ) + _T("\n");
  }
  SendCmd( _T("SETSCRIPTTAGS"), cmd );
}


void TASServer::SendRaw( const wxString& raw )
{
  SendCmd( raw );
}


void TASServer::RequestInGameTime( const wxString& nick )
{
  SendCmd( _T("GETINGAMETIME"), nick );
}


Battle* TASServer::GetCurrentBattle()
{
  //ASSERT_LOGIC( m_battle_id != -1, _T("invalid m_battle_id value") );
  if(m_battle_id==-1) return NULL;
  if(!BattleExists(m_battle_id))return NULL;
  return &GetBattle( m_battle_id );
}


void TASServer::SendMyBattleStatus( UserBattleStatus& bs )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  GetMe().SetBattleStatus( bs );

  UTASBattleStatus tasbs;
  tasbs.tasdata = ConvTasbattlestatus( bs );
  UTASColor tascl;
  tascl.color.red = bs.color_r;
  tascl.color.green = bs.color_g;
  tascl.color.blue = bs.color_b;
  tascl.color.zero = 0;
  //MYBATTLESTATUS battlestatus myteamcolor
  SendCmd( _T("MYBATTLESTATUS"), wxString::Format( _T("%d %d"), tasbs.data, tascl.data ) );
}


void TASServer::SendMyUserStatus()
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );
  UserStatus& us = GetMe().Status();

  UTASClientStatus taus;
  taus.tasdata.in_game = us.in_game;
  taus.tasdata.away = us.away;
  taus.tasdata.rank = us.rank;
  taus.tasdata.moderator = us.moderator;
  taus.tasdata.bot = us.bot;

  SendCmd( _T("MYSTATUS"), wxString::Format( _T("%d"), taus.byte ) );
}


void TASServer::StartHostedBattle()
{
  ASSERT_LOGIC( m_battle_id != -1, _T("Invalid m_battle_id") );
  m_se->OnStartHostedBattle( m_battle_id );
}


void TASServer::ForceSide( int battleid, const wxString& nick, int side )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( battleid == m_battle_id, _T("Not current battle") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  if ( nick == GetMe().GetNick() ) {
    GetMe().BattleStatus().side = side;
    SendMyBattleStatus( GetMe().BattleStatus() );
  } else {
    try {
      DoActionBattle( battleid, _T("suggests that ") + nick + _T(" changes to ") + usync()->GetSideName( GetBattle(battleid).GetModName(), side ) + _T(" side.") );
    } catch (...) {}
  }
}


void TASServer::ForceTeam( int battleid, const wxString& nick, int team )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( battleid == m_battle_id, _T("Not current battle") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  if ( !GetBattle(battleid).IsFounderMe() ) {
    if ( nick == GetMe().GetNick() ) {
      GetMe().BattleStatus().team = team;
      SendMyBattleStatus( GetMe().BattleStatus() );
    } else {
      DoActionBattle( battleid, _T("suggests that ") + nick + _T(" changes to team #") + wxString::Format( _T("%d"), team + 1 ) + _T(".") );
    }
    return;
  }

  //FORCETEAMNO username teamno
  SendCmd( _T("FORCETEAMNO"), nick + wxString::Format(_T(" %d"), team ) );
}


void TASServer::ForceAlly( int battleid, const wxString& nick, int ally )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( battleid == m_battle_id, _T("Not current battle") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  if ( !GetBattle(battleid).IsFounderMe() ) {
    if ( nick == GetMe().GetNick() ) {
      GetMe().BattleStatus().ally = ally;
      SendMyBattleStatus( GetMe().BattleStatus() );
    } else {
      DoActionBattle( battleid, _T("suggests that ") + nick + _T(" changes to ally #") + wxString::Format( _T("%d"), ally + 1 ) + _T(".") );
    }
    return;
  }

  //FORCEALLYNO username teamno
  SendCmd( _T("FORCEALLYNO"), nick + wxString::Format( _T(" %d"), ally ) );
}


void TASServer::ForceColour( int battleid, const wxString& nick, int r, int g, int b )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( battleid == m_battle_id, _T("Not current battle") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  if ( !GetBattle(battleid).IsFounderMe() ) {
    if ( nick == GetMe().GetNick() ) {
      GetMe().BattleStatus().color_r = r;
      GetMe().BattleStatus().color_g = g;
      GetMe().BattleStatus().color_b = b;
      SendMyBattleStatus( GetMe().BattleStatus() );
    } else {
      DoActionBattle( battleid, _T("sugests that ") + nick + _T(" changes colour.") );
    }
    return;
  }

  UTASColor tascl;
  tascl.color.red = r;
  tascl.color.green = g;
  tascl.color.blue = b;
  tascl.color.zero = 0;
  //FORCETEAMCOLOR username color
  SendCmd( _T("FORCETEAMCOLOR"), nick + _T(" ") + wxString::Format( _T("%d"), tascl.data ) );
}


void TASServer::ForceSpectator( int battleid, const wxString& nick, bool spectator )
{
  wxLogDebugFunc( _T("") );

  if ( !GetBattle(battleid).IsFounderMe()) {
    if ( nick == GetMe().GetNick() ) {
      GetMe().BattleStatus().spectator = spectator;
      SendMyBattleStatus( GetMe().BattleStatus() );
    } else {
      if ( spectator ) DoActionBattle( battleid, _T("suggests that ") + nick + _T(" becomes a spectator.") );
      else DoActionBattle( battleid, _T("suggests that ") + nick + _T(" plays.") );
    }
    return;
  }

  if ( !spectator ) {
    if ( nick == GetMe().GetNick() ) {
      GetMe().BattleStatus().spectator = spectator;
      SendMyBattleStatus( GetMe().BattleStatus() );
    } else {
      DoActionBattle( battleid, _T("suggests that ") + nick + _T(" plays.") );
    }
    return;
  }

  ASSERT_LOGIC( battleid == m_battle_id, _T("Not current battle") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  //FORCESPECTATORMODE username
  SendCmd( _T("FORCESPECTATORMODE"), nick );
}


void TASServer::BattleKickPlayer( int battleid, const wxString& nick )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( battleid == m_battle_id, _T("Not current battle") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  if ( !GetBattle(battleid).IsFounderMe() ) {
    if ( nick == GetMe().GetNick() ) {
      LeaveBattle( battleid );
    } else {
      DoActionBattle( battleid, _T("thinks ") + nick + _T(" should leave.") );
    }
    return;
  }

  //KICKFROMBATTLE username
  SendCmd( _T("KICKFROMBATTLE"), nick );
}

void TASServer::SetHandicap( int battleid, const wxString& nick, int handicap)
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( battleid == m_battle_id, _T("Not current battle") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );
  if ( !GetBattle(battleid).IsFounderMe() ) {
    DoActionBattle( battleid, _T("thinks ") + nick + _T(" should get a ") + wxString::Format( _T("%d"), handicap) + _T("% resource bonus") );
    return;
  }

  //HANDICAP username value
  SendCmd( _T("HANDICAP"), nick + wxString::Format( _T(" %d"), handicap ) );
}


void TASServer::AddBot( int battleid, const wxString& nick, const wxString& owner, UserBattleStatus status, const wxString& aidll )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  UTASBattleStatus tasbs;
  tasbs.tasdata = ConvTasbattlestatus( status );
  UTASColor tascl;
  tascl.color.red = status.color_r;
  tascl.color.green = status.color_g;
  tascl.color.blue = status.color_b;
  tascl.color.zero = 0;
  //ADDBOT name battlestatus teamcolor {AIDLL}
  SendCmd( _T("ADDBOT"), nick + wxString::Format( _T(" %d %d "), tasbs.data, tascl.data ) + aidll + _T(".dll") );
}


void TASServer::RemoveBot( int battleid, const wxString& nick )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  Battle& battle = GetBattle( battleid );
  BattleBot* bot = battle.GetBot( nick );
  ASSERT_LOGIC( bot != 0, _T("Bot does not exist.") );

  if (!( battle.IsFounderMe() || ( bot->owner == GetMe().GetNick() ) )) {
    DoActionBattle( battleid, _T("thinks the bot ") + nick + _T(" should be removed.") );
    return;
  }

  //REMOVEBOT name
  SendCmd( _T("REMOVEBOT"), nick );
}


void TASServer::UpdateBot( int battleid, const wxString& nick, UserBattleStatus status )
{
  wxLogDebugFunc( _T("") );
  ASSERT_LOGIC( IsOnline(), _T("Not online") );
  ASSERT_LOGIC( m_sock != 0, _T("m_sock = 0") );

  UTASBattleStatus tasbs;
  tasbs.tasdata = ConvTasbattlestatus( status );
  UTASColor tascl;
  tascl.color.red = status.color_r;
  tascl.color.green = status.color_g;
  tascl.color.blue = status.color_b;
  tascl.color.zero = 0;
  //UPDATEBOT name battlestatus teamcolor
  SendCmd( _T("UPDATEBOT"), nick + wxString::Format( _T(" %d %d"), tasbs.data, tascl.data ) );
}


void TASServer::OnConnected( Socket* sock )
{
  wxLogDebugFunc( _T("") );
  //TASServer* serv = (TASServer*)sock->GetUserdata();
  m_last_ping = time( 0 );
  m_connected = true;
  m_online = false;
}


void TASServer::OnDisconnected( Socket* sock )
{
  wxLogDebugFunc( _T("") );
  m_connected = false;
  m_online = false;
  m_se->OnDisconnected();
}


void TASServer::OnDataReceived( Socket* sock )
{
  //TASServer* serv = (TASServer*)sock->GetUserdata();
  ReceiveAndExecute();
}

bool TASServer::TestOpenPort( unsigned int port )
{
  return m_sock->TestOpenPort( Udp, port );
}

////////////////////////
// Utility functions
//////////////////////

UserStatus ConvTasclientstatus( TASClientstatus tas )
{
  UserStatus stat;
  stat.in_game = tas.in_game;
  stat.away = tas.away;
  stat.rank = (tas.rank + 1) * 100;
  stat.moderator = tas.moderator;
  stat.bot = tas.bot;
  return stat;
}

UserBattleStatus ConvTasbattlestatus( TASBattleStatus tas )
{
  UserBattleStatus stat;
  stat.ally = tas.ally;
  stat.handicap = tas.handicap;
  stat.ready = (tas.ready==1)?true:false;
  stat.side = tas.side;
  stat.spectator = (tas.player == 0)?true:false;
  stat.sync = tas.sync;
  stat.team = tas.team;
  return stat;
}


TASBattleStatus ConvTasbattlestatus( UserBattleStatus bs)
{
  TASBattleStatus stat;
  stat.ally = bs.ally;
  stat.handicap = bs.handicap;
  stat.ready = bs.ready?1:0;
  stat.side = bs.side;
  stat.player = bs.spectator?0:1;
  stat.sync = bs.sync;
  stat.team = bs.team;
  return stat;
}


StartType IntToStartType( int start )
{
  switch ( start ) {
    case 0: return ST_Fixed;
    case 1: return ST_Random;
    case 2: return ST_Choose;
    default: ASSERT_LOGIC( false, _T("invalid value") );
  };
  return ST_Fixed;
}


NatType IntToNatType( int nat )
{
  switch ( nat ) {
    case 0: return NAT_None;
    case 1: return NAT_Hole_punching;
    case 2: return NAT_Fixed_source_ports;
    default: ASSERT_LOGIC( false, _T("invalid value") );
  };
  return NAT_None;
}


GameType IntToGameType( int gt )
{
  switch ( gt ) {
    case 0: return GT_ComContinue;
    case 1: return GT_ComEnds;
    case 2: return GT_Lineage;
    default: ASSERT_LOGIC( false, _T("invalid value") );
  };
  return GT_ComContinue;
}
