
namespace bspguy {

	const int FL_EQUIP_ALL_ON_USE = 1;
	const int FL_FORCE_WEAPON_SWITCH = 2; // force switch to most powerful weapon on trigger
	const int FL_REQUIP_ON_USE = 4;
	const int FL_ALLOW_MULTI_USE_PER_LIFE = 8;

	enum respawn_equip_modes {
		RESPAWN_EQUIP_IF_ON,
		RESPAWN_EQUIP_ALWAYS
	}
	
	enum ammo_equip_modes {
		AMMO_EQUIP_RESTOCK,
		AMMO_EQUIP_ADD
	}

	class EquipItem {
		string classname;
		int primaryAmmo = 0;
		int secondaryAmmo = 0;
		bool isWeapon = true;
		
		EquipItem() {}
		
		EquipItem(string classname) {
			this.classname = classname;
		}
	}
	
	array<EHandle> g_equip_ents;
	bool g_equipHookRegistered = false;

	HookReturnCode EquipPlayerSpawn(CBasePlayer@ plr) {
		for (uint i = 0; i < g_equip_ents.size(); i++) {
			if (g_equip_ents[i].IsValid()) {
				bspguy_equip@ equip = cast<bspguy_equip@>(CastToScriptClass(g_equip_ents[i].GetEntity()));
				equip.playerUsedAlready[plr.entindex()] = false;
				if (equip.applyToSpawners)
					equip.equip_player(plr, true); // bpyass one-use-per-life limit
			}
		}
		return HOOK_CONTINUE;
	}

	class bspguy_equip : ScriptBaseEntity
	{	
		array<EquipItem> items;
		array<bool> playerUsedAlready;
		bool oneUsePerLife = true;
		bool applyToSpawners = false;
		int respawn_equip_mode = RESPAWN_EQUIP_IF_ON;
		int ammo_equip_mode = AMMO_EQUIP_RESTOCK;
		
		float newMaxHealth = 0;
		float newMaxArmor = 0;
		float setHealth = 0;
		float setArmor = 0;
		
		string best_weapon = "";
		
		bool stripSuit = false;
		
		bool KeyValue( const string& in szKey, const string& in szValue )
		{
			if (szKey == "set_max_health") {
				newMaxHealth = atof(szValue);
			}
			else if (szKey == "set_max_armor") {
				newMaxArmor = atof(szValue);
			}
			else if (szKey == "set_armor") {
				setArmor = atof(szValue);
			}
			else if (szKey == "set_health") {
				setHealth = atof(szValue);
			}
			else if (szKey == "nosuit") {
				stripSuit = true;
			}
			else if (szKey == "respawn_equip_mode") {
				respawn_equip_mode = atoi(szValue);
				if (respawn_equip_mode == RESPAWN_EQUIP_ALWAYS) {
					applyToSpawners = true;
				}
			}
			else if (szKey == "best_weapon") {
				best_weapon = szValue;
			}
			else if (szKey.Find("weapon_") == 0) {
				EquipItem item = EquipItem(szKey);
				item.isWeapon = true;
				
				string primaryAmmo = "0";
				string secondaryAmmo = "0";
				
				int ammoSep = szValue.Find("+");
				if (ammoSep != -1) {
					item.primaryAmmo = atoi( szValue.SubString(0, ammoSep) );
					item.secondaryAmmo = atoi( szValue.SubString(ammoSep+1) );
				} else {
					item.primaryAmmo = atoi(szValue);
				}
				
				items.insertLast(item);
			}
			else if (szKey.Find("item_") == 0) {
				EquipItem item = EquipItem(szKey);
				item.isWeapon = false;				
				items.insertLast(item);
			}
			return BaseClass.KeyValue( szKey, szValue );
		}
		
		void Spawn()
		{
			if (!g_equipHookRegistered) {
				g_Hooks.RegisterHook( Hooks::Player::PlayerSpawn, @EquipPlayerSpawn );
				g_equipHookRegistered = true;
			}
				
			playerUsedAlready.resize(33);
			
			g_equip_ents.insertLast(self);
		}

		void Use(CBaseEntity@ pActivator, CBaseEntity@ pCaller, USE_TYPE useType, float flValue = 0.0f)
		{
			oneUsePerLife = pev.spawnflags & FL_ALLOW_MULTI_USE_PER_LIFE == 0;
			
			if (!applyToSpawners && useType == USE_ON) {
				playerUsedAlready.resize(0);
				playerUsedAlready.resize(33);
			}
			
			if (useType == USE_ON) {
				applyToSpawners = true;
			} else if (useType == USE_OFF && respawn_equip_mode != RESPAWN_EQUIP_ALWAYS) {
				applyToSpawners = false;
				return;
			}
			
			if (pev.spawnflags & FL_EQUIP_ALL_ON_USE != 0) {
				for ( int i = 1; i <= g_Engine.maxClients; i++ )
				{
					CBasePlayer@ plr = g_PlayerFuncs.FindPlayerByIndex(i);
					if (plr is null or !plr.IsConnected())
						continue;
					
					if (pev.spawnflags & FL_REQUIP_ON_USE != 0) {
						plr.RemoveAllItems(stripSuit);
						plr.SetItemPickupTimes(0);
					}
					
					if (oneUsePerLife && playerUsedAlready[plr.entindex()]) {
						return;
					}
					
					if (equip_player(plr, pev.spawnflags & FL_FORCE_WEAPON_SWITCH != 0)) {
						g_SoundSystem.EmitSoundDyn(plr.edict(), CHAN_ITEM, "items/gunpickup2.wav", 1.0f, 1.0f);
					}
				}
				
				return;
			}
			
			if (pActivator is null or !pActivator.IsPlayer()) {
				return;
			}
			
			CBasePlayer@ plr = cast<CBasePlayer@>(pActivator);
			
			if (oneUsePerLife && playerUsedAlready[plr.entindex()]) {
				return;
			}
			
			if (pev.spawnflags & FL_REQUIP_ON_USE != 0) {
				plr.RemoveAllItems(stripSuit);
				plr.SetItemPickupTimes(0);
			}
			
			if (equip_player(plr, pev.spawnflags & FL_FORCE_WEAPON_SWITCH != 0)) {
				g_SoundSystem.EmitSoundDyn(plr.edict(), CHAN_ITEM, "items/gunpickup2.wav", 1.0f, 1.0f);
			}
		}
		
		bool equip_player(CBasePlayer@ plr, bool switchWeapon) {
			bool anyWeaponGiven = false;
			
			for (uint i = 0; i < items.size(); i++) {
				if (!items[i].isWeapon) {
					plr.GiveNamedItem(items[i].classname);
					continue;
				}
				
				CBasePlayerWeapon@ wep = cast<CBasePlayerWeapon@>(plr.HasNamedPlayerItem(items[i].classname));
				
				if (wep is null) {
					dictionary keys;
					keys["origin"] = plr.pev.origin.ToString();
					keys["spawnflags"] = "1024";
					@wep = cast<CBasePlayerWeapon@>(g_EntityFuncs.CreateEntity(items[i].classname, keys, true));
					
					if (wep is null) {
						println("bspguy_equip: Invalid weapon class " + items[i].classname);
						continue;
					}
					
					// using wrong classname works with CreateEntity but not HasNamedPlayerItem
					// delete the new weapon if the player actually has the same item by a different name
					bool alreadyHadWeapon = false;
					if (wep.pev.classname != items[i].classname) {
						CBasePlayerWeapon@ oldWep = cast<CBasePlayerWeapon@>(plr.HasNamedPlayerItem(wep.pev.classname));
						if (oldWep !is null) {
							alreadyHadWeapon = true;
							g_EntityFuncs.Remove(wep);
							@wep = @oldWep;
						}
					}
					
					if (!alreadyHadWeapon) {
						wep.m_iDefaultAmmo = 0;
						plr.SetItemPickupTimes(0);
						wep.Touch(plr);
						
						if (wep.m_iClip != -1) {
							wep.m_iClip = wep.iMaxClip();
						}
						
						anyWeaponGiven = true;
					}
				}
				
				int primaryAmmoIdx = wep.PrimaryAmmoIndex();
				if (primaryAmmoIdx != -1) {
					int newAmmo = plr.m_rgAmmo(primaryAmmoIdx) + items[i].primaryAmmo;
					
					if (ammo_equip_mode == AMMO_EQUIP_RESTOCK) {
						newAmmo = Math.max(items[i].primaryAmmo, plr.m_rgAmmo(primaryAmmoIdx));
					}
					
					int maxAmmo = plr.GetMaxAmmo(primaryAmmoIdx);
					plr.m_rgAmmo(primaryAmmoIdx, Math.min(newAmmo, maxAmmo));
				}
				
				int secondaryAmmoIdx = wep.SecondaryAmmoIndex();
				if (secondaryAmmoIdx != -1) {
					int newAmmo = plr.m_rgAmmo(secondaryAmmoIdx) + items[i].secondaryAmmo;
					
					if (ammo_equip_mode == AMMO_EQUIP_RESTOCK) {
						newAmmo = Math.max(items[i].primaryAmmo, plr.m_rgAmmo(primaryAmmoIdx));
					}
					
					int maxAmmo = plr.GetMaxAmmo(secondaryAmmoIdx);
					plr.m_rgAmmo(secondaryAmmoIdx, Math.min(newAmmo, maxAmmo));
				}
			}
			
			// select the best weapon
			bool forceWeaponSwitch = pev.spawnflags & FL_FORCE_WEAPON_SWITCH != 0;
			if (switchWeapon && (anyWeaponGiven || forceWeaponSwitch)) {
				CBasePlayerWeapon@ bestWeapon = null;
				int bestWeight = -1;
			
				for (uint i = 0; i < MAX_ITEM_TYPES; i++) {
					CBasePlayerWeapon@ wep = cast<CBasePlayerWeapon@>(plr.m_rgpPlayerItems(i));
					
					if (wep !is null) {
						if (best_weapon == string(wep.pev.classname)) {
							@bestWeapon = @wep;
							break;
						}
					
						ItemInfo itemInfo;
						wep.GetItemInfo(itemInfo);
						if (itemInfo.iWeight > bestWeight) {
							bestWeight = itemInfo.iWeight;
							@bestWeapon = @wep;
						}
					}
				}
				
				if (bestWeapon !is null) {
					plr.SwitchWeapon(bestWeapon);
				}
			}
			
			playerUsedAlready[plr.entindex()] = true;
			
			if (newMaxArmor > 0) {
				plr.pev.armortype = newMaxArmor;
			}
			if (newMaxHealth > 0) {
				plr.pev.max_health = newMaxHealth;
			}
			if (setHealth > 0) {
				plr.pev.health = setHealth;
			}
			if (setArmor > 0) {
				plr.pev.armorvalue = setArmor;
			}
			
			return anyWeaponGiven;
		}
	}
}