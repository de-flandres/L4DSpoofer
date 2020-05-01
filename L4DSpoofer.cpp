//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//

//09553800 | 8B 44 24 1C | mov eax, dword ptr ss : [esp + 1C] | - InitiateConnection function
//\x8B\x44\x24\x1C\x56\x8B\x31\x85\xF6\x88\x00\x00\x00\x00\x00\x75\x06
//xxxxxxxxxx?????xx

//095EBCC0 | E8 3B 7B 02 00           | call <engine.sub_9613800> - InitiateConnection call
//\xE8\x00\x00\x00\x00\x8B\x00\x00\x00\x00\x00\x00\x8B\xF0\x56\x8B\xCF
//x????x??????xxxxx

#pragma comment ( lib , "legacy_stdio_definitions.lib" );

#include <stdio.h>
#include <Windows.h>
#include <inttypes.h>

#include "interface.h"
#include "filesystem.h"
#include "engine/iserverplugin.h"
#include "game/server/iplayerinfo.h"
#include "eiface.h"
#include "igameevents.h"
#include "convar.h"
#include "Color.h"
#include "vstdlib/random.h"
#include "engine/IEngineTrace.h"
#include "tier2/tier2.h"
//#include "convar.h"
#include "tier1/iconvar.h"

#include "Utils/FindPattern.h"
#include "Utils/XMemory.h"
#include "Emulators/RevEmu2013.h"

ConVar SteamID("l4d_steam_id", "3333333", FCVAR_ARCHIVE, "");
ConVar L4DSpoofSteamIDEnabled("l4d_spoof_steam_id_enabled", "0", FCVAR_ARCHIVE, "");

// Interfaces from the engine
IVEngineServer	*engine = NULL; // helper functions (messaging clients, loading content, making entities, running commands, etc)

typedef int (*FuncType)(void* pData, int cbMaxData, uint32 unIP, uint16 usPort, uint64 unGSSteamID, bool bSecure);
FuncType CSteam3Client_InitiateConnection_original;

int __stdcall hkCSteam3Client_InitiateConnection(void* pData, int cbMaxData, uint32 unIP, uint16 usPort, uint64 unGSSteamID, bool bSecure) {
	
	if (L4DSpoofSteamIDEnabled.GetBool()) {
		long nSteamID = (SteamID.GetInt() == 0) ? (__rdtsc() % INT_MAX) : (SteamID.GetInt());

		int nOutSize = GenerateRevEmu2013(pData, SteamID.GetInt());
		if (nOutSize == 0)
		{
			ConColorMsg(Color(255, 0, 0, 255), "CSteam3Client::InitiateConnection - Could not change SteamID value!\n");
			return 0;
		}

		ConColorMsg(Color(0, 255, 0, 255), "CSteam3Client::InitiateConnection - SteamID successfully changed to %d.\n", nSteamID);

		//
		// Modern Source Engine builds require that SteamID value was in
		// the beginning of ticket.
		//

		auto pdata = (long*)pData;
		pdata[0] = (nSteamID & 1); // SteamID, low part
		pdata[1] = 0x01100001;
		return nOutSize;
	}
	
	if (!L4DSpoofSteamIDEnabled.GetBool() && CSteam3Client_InitiateConnection_original) {
		int num = CSteam3Client_InitiateConnection_original(pData, cbMaxData, unIP, usPort, unGSSteamID, bSecure);
		return num;
	}
}

void HookInitiateConnection() {
	auto InitiateConnection_call_adr = FindPattern("engine.dll", "\xE8\x00\x00\x00\x00\x8B\x00\x00\x00\x00\x00\x00\x8B\xF0\x56\x8B\xCF", "x????x??????xxxxx");
	auto InitiateConnection_call_func = FindPattern("engine.dll", "\x8B\x44\x24\x1C\x56\x8B\x31\x85\xF6\x88\x00\x00\x00\x00\x00\x75\x06", "xxxxxxxxxx?????xx");

	if (!InitiateConnection_call_adr) {
		ConColorMsg(Color(255, 0, 0, 255), "Cant find CSteam3Client::InitiateConnection call!\n");
		return;
	}
	else {
		ConColorMsg(Color(0, 255, 0, 255), "CSteam3Client::InitiateConnection call- 0x%x\n", InitiateConnection_call_adr);
	}

	if (!InitiateConnection_call_func) {
		ConColorMsg(Color(255, 0, 0, 255), "Cant find CSteam3Client::InitiateConnection function!\n");
		return;
	}
	else {
		CSteam3Client_InitiateConnection_original = (FuncType)InitiateConnection_call_func;
		ConColorMsg(Color(0, 255, 0, 255), "CSteam3Client::InitiateConnection function - 0x%x\n", InitiateConnection_call_func);		
	}

	auto call = Transpose((void*)InitiateConnection_call_adr, 0);
	InsertCall(call, hkCSteam3Client_InitiateConnection);
	ConColorMsg(Color(0, 255, 0, 255), "CSteam3Client::InitiateConnection - hooked!\n");	
}

class L4DSpoofer: public IServerPluginCallbacks, public IGameEventListener
{
public:
	L4DSpoofer();
	~L4DSpoofer();

	// IServerPluginCallbacks methods
	virtual bool			Load(	CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory );
	virtual void			Unload( void );
	virtual void			Pause( void );
	virtual void			UnPause( void );
	virtual const char     *GetPluginDescription( void );      
	virtual void			LevelInit( char const *pMapName );
	virtual void			ServerActivate( edict_t *pEdictList, int edictCount, int clientMax );
	virtual void			GameFrame( bool simulating );
	virtual void			LevelShutdown( void );
	virtual void			ClientActive( edict_t *pEntity );
	virtual void			ClientDisconnect( edict_t *pEntity );
	virtual void			ClientPutInServer( edict_t *pEntity, char const *playername );
	virtual void			SetCommandClient( int index );
	virtual void			ClientSettingsChanged( edict_t *pEdict );
	virtual PLUGIN_RESULT	ClientConnect( bool *bAllowConnect, edict_t *pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen );
	virtual PLUGIN_RESULT	ClientCommand( edict_t *pEntity, const CCommand &args );
	virtual PLUGIN_RESULT	NetworkIDValidated( const char *pszUserName, const char *pszNetworkID );
	virtual void			OnQueryCvarValueFinished( QueryCvarCookie_t iCookie, edict_t *pPlayerEntity, EQueryCvarValueStatus eStatus, const char *pCvarName, const char *pCvarValue );

	// IGameEventListener Interface
	virtual void FireGameEvent( KeyValues * event );

	virtual int GetCommandIndex() { return m_iClientCommandIndex; }
private:
	int m_iClientCommandIndex;
};


// 
// The plugin is a static singleton that is exported as an interface
//
L4DSpoofer g_EmtpyServerPlugin;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(L4DSpoofer, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_EmtpyServerPlugin );

//---------------------------------------------------------------------------------
// Purpose: constructor/destructor
//---------------------------------------------------------------------------------
L4DSpoofer::L4DSpoofer()
{
	m_iClientCommandIndex = 0;
}

L4DSpoofer::~L4DSpoofer()
{
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is loaded, load the interface we need from the engine
//---------------------------------------------------------------------------------
bool L4DSpoofer::Load(	CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory )
{
	ConnectTier1Libraries( &interfaceFactory, 1 );
	ConnectTier2Libraries( &interfaceFactory, 1 );

	engine = (IVEngineServer*)interfaceFactory(INTERFACEVERSION_VENGINESERVER, NULL);

	ConColorMsg(Color(0, 255, 0, 255), "L4D Spoofer Loaded Successfully.\n");

	//MathLib_Init( 2.2f, 2.2f, 0.0f, 2 );
	ConVar_Register( 0 );

	HookInitiateConnection();
	return true;
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is unloaded (turned off)
//---------------------------------------------------------------------------------
void L4DSpoofer::Unload( void )
{
	//gameeventmanager->RemoveListener( this ); // make sure we are unloaded from the event system

	ConVar_Unregister( );
	DisconnectTier2Libraries( );
	DisconnectTier1Libraries( );
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is paused (i.e should stop running but isn't unloaded)
//---------------------------------------------------------------------------------
void L4DSpoofer::Pause( void )
{
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is unpaused (i.e should start executing again)
//---------------------------------------------------------------------------------
void L4DSpoofer::UnPause( void )
{
}

//---------------------------------------------------------------------------------
// Purpose: the name of this plugin, returned in "plugin_print" command
//---------------------------------------------------------------------------------
const char *L4DSpoofer::GetPluginDescription( void )
{
	return "L4D Spoofer, Senny";
}

//---------------------------------------------------------------------------------
// Purpose: called on level start
//---------------------------------------------------------------------------------
void L4DSpoofer::LevelInit( char const *pMapName )
{	
	//gameeventmanager->AddListener( this, true );
}

//---------------------------------------------------------------------------------
// Purpose: called on level start, when the server is ready to accept client connections
//		edictCount is the number of entities in the level, clientMax is the max client count
//---------------------------------------------------------------------------------
void L4DSpoofer::ServerActivate( edict_t *pEdictList, int edictCount, int clientMax )
{
}

//---------------------------------------------------------------------------------
// Purpose: called once per server frame, do recurring work here (like checking for timeouts)
//---------------------------------------------------------------------------------
void L4DSpoofer::GameFrame( bool simulating )
{
}

//---------------------------------------------------------------------------------
// Purpose: called on level end (as the server is shutting down or going to a new map)
//---------------------------------------------------------------------------------
void L4DSpoofer::LevelShutdown( void ) // !!!!this can get called multiple times per map change
{
	//gameeventmanager->RemoveListener( this );
}

//---------------------------------------------------------------------------------
// Purpose: called when a client spawns into a server (i.e as they begin to play)
//---------------------------------------------------------------------------------
void L4DSpoofer::ClientActive( edict_t *pEntity )
{
}

//---------------------------------------------------------------------------------
// Purpose: called when a client leaves a server (or is timed out)
//---------------------------------------------------------------------------------
void L4DSpoofer::ClientDisconnect( edict_t *pEntity )
{
}

//---------------------------------------------------------------------------------
// Purpose: called on 
//---------------------------------------------------------------------------------
void L4DSpoofer::ClientPutInServer( edict_t *pEntity, char const *playername )
{
}

//---------------------------------------------------------------------------------
// Purpose: called on level start
//---------------------------------------------------------------------------------
void L4DSpoofer::SetCommandClient( int index )
{
}

//---------------------------------------------------------------------------------
// Purpose: called on level start
//---------------------------------------------------------------------------------
void L4DSpoofer::ClientSettingsChanged( edict_t *pEdict )
{
}

//---------------------------------------------------------------------------------
// Purpose: called when a client joins a server
//---------------------------------------------------------------------------------
PLUGIN_RESULT L4DSpoofer::ClientConnect( bool *bAllowConnect, edict_t *pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen )
{
	return PLUGIN_CONTINUE;
}

//---------------------------------------------------------------------------------
// Purpose: called when a client types in a command (only a subset of commands however, not CON_COMMAND's)
//---------------------------------------------------------------------------------
PLUGIN_RESULT L4DSpoofer::ClientCommand( edict_t *pEntity, const CCommand &args )
{
	return PLUGIN_CONTINUE;
}

//---------------------------------------------------------------------------------
// Purpose: called when a client is authenticated
//---------------------------------------------------------------------------------
PLUGIN_RESULT L4DSpoofer::NetworkIDValidated( const char *pszUserName, const char *pszNetworkID )
{
	return PLUGIN_CONTINUE;
}

//---------------------------------------------------------------------------------
// Purpose: called when a cvar value query is finished
//---------------------------------------------------------------------------------
void L4DSpoofer::OnQueryCvarValueFinished( QueryCvarCookie_t iCookie, edict_t *pPlayerEntity, EQueryCvarValueStatus eStatus, const char *pCvarName, const char *pCvarValue )
{
}

//---------------------------------------------------------------------------------
// Purpose: called when an event is fired
//---------------------------------------------------------------------------------
void L4DSpoofer::FireGameEvent( KeyValues * event )
{
	//const char* name = event->GetName();
	//Msg("\nFireGameEvent: Got event \"%s\"\n", name);
}

//---------------------------------------------------------------------------------
// Purpose: an example of how to implement a new command
//---------------------------------------------------------------------------------
CON_COMMAND( l4d_spoofer_version, "prints the version of the plugin" )
{
	Msg( "Version: 1.0\n" );
}

CON_COMMAND(l4d_spoof_steamid, "")
{
	HookInitiateConnection();
}

//static ConVar empty_cvar("plugin_empty", "0", 0, "Example plugin cvar");