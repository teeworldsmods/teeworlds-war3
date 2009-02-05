/* copyright (c) 2007 rajh */
#include <engine/e_server_interface.h>
#include <game/mapitems.hpp>
#include <game/server/entities/character.hpp>
#include <game/server/player.hpp>
#include <game/server/gamecontext.hpp>
#include <engine/e_server_interface.h>
#include <engine/e_config.h>
#include "war3.hpp"
#include "ctf.hpp"
#include <string.h>
#include <stdio.h>

GAMECONTROLLER_WAR::GAMECONTROLLER_WAR()
{
	gametype = "WAR3";
	flags[0] = 0;
	flags[1] = 0;
	game_flags = GAMEFLAG_TEAMS|GAMEFLAG_FLAGS;
	//Init xp table
	lvlmap[0]=0; /* Not used */
	lvlmap[1]=10;
	lvlmap[2]=50;
	lvlmap[3]=100;
	lvlmap[4]=200;
	lvlmap[5]=300;
	lvlmap[6]=500;
	lvlmap[7]=700;
	load_xp_table();
}

bool GAMECONTROLLER_WAR::on_entity(int index, vec2 pos)
{
	if(GAMECONTROLLER::on_entity(index, pos))
		return true;
	
	int team = -1;
	if(index == ENTITY_FLAGSTAND_RED) team = 0;
	if(index == ENTITY_FLAGSTAND_BLUE) team = 1;
	if(team == -1)
		return false;
		
	FLAG *f = new FLAG(team);
	f->stand_pos = pos;
	f->pos = pos;
	flags[team] = f;
	return true;
}

int GAMECONTROLLER_WAR::on_character_death(class CHARACTER *victim, class PLAYER *killer, int weaponid)
{
	vec2 tempPos=victim->core.pos;
	GAMECONTROLLER::on_character_death(victim, killer, weaponid);

	//Xp for killing
	if(killer && killer->client_id != victim->player->client_id)
		killer->xp+=(victim->player->lvl*3);

	int had_flag = 0;
	
	// drop flags
	for(int fi = 0; fi < 2; fi++)
	{
		FLAG *f = flags[fi];
		if(f && killer && f->carrying_character == killer->get_character())
			had_flag |= 2;
		if(f && f->carrying_character == victim)
		{
			game.create_sound_global(SOUND_CTF_DROP);
			f->drop_tick = server_tick();
			f->carrying_character = 0;
			f->vel = vec2(0,0);
			
			if(killer && killer->team != victim->team)
				killer->score++;
				
			had_flag |= 1;
		}
	}

	//Exploding undead
	if(victim->player && victim->player->undead_special && !victim->player->special_used)
	{
		victim->player->special_used=true;
		victim->player->exploded=true;
		victim->player->special_used_tick=server_tick()+server_tickspeed()*config.sv_specialtime*3;
		game.create_explosion(tempPos, victim->player->client_id, WEAPON_EXPLODE, false);
	}
	else if(victim->player &&  !victim->player->undead_special)
	{
		victim->player->special_used=false;
	}
	victim->player->invincible_used=false;

	return had_flag;
}

void GAMECONTROLLER_WAR::tick()
{
	GAMECONTROLLER::tick();

	do_team_score_wincheck();
	
	for(int fi = 0; fi < 2; fi++)
	{
		FLAG *f = flags[fi];
		
		if(!f)
			continue;
		
		// flag hits death-tile, reset it
		if(col_get((int)f->pos.x, (int)f->pos.y)&COLFLAG_DEATH)
		{
			game.create_sound_global(SOUND_CTF_RETURN);
			f->reset();
			continue;
		}
		
		//
		if(f->carrying_character)
		{
			// update flag position
			f->pos = f->carrying_character->pos;
			
			if(flags[fi^1] && flags[fi^1]->at_stand)
			{
				if(distance(f->pos, flags[fi^1]->pos) < 32)
				{
					// CAPTURE! \o/
					teamscore[fi^1] += 100;
					f->carrying_character->player->score += 5;
					//Xp
					f->carrying_character->player->xp += 50;

					dbg_msg("game", "flag_capture player='%d:%s'",
						f->carrying_character->player->client_id,
						server_clientname(f->carrying_character->player->client_id));

					char buf[512];
					float capture_time = (server_tick() - f->grab_tick)/(float)server_tickspeed();
					if(capture_time <= 60)
					{
						str_format(buf, sizeof(buf), "the %s flag was captured by %s (%d.%s%d seconds)", fi ? "blue" : "red", server_clientname(f->carrying_character->player->client_id), (int)capture_time%60, ((int)(capture_time*100)%100)<10?"0":"", (int)(capture_time*100)%100);
					}
					else
					{
						str_format(buf, sizeof(buf), "the %s flag was captured by %s", fi ? "blue" : "red", server_clientname(f->carrying_character->player->client_id));
					}
					game.send_chat(-1, -2, buf);
					for(int i = 0; i < 2; i++)
						flags[i]->reset();
					
					game.create_sound_global(SOUND_CTF_CAPTURE);
				}
			}			
		}
		else
		{
			CHARACTER *close_characters[MAX_CLIENTS];
			int num = game.world.find_entities(f->pos, 32.0f, (ENTITY**)close_characters, MAX_CLIENTS, NETOBJTYPE_CHARACTER);
			for(int i = 0; i < num; i++)
			{
				if(!close_characters[i]->alive || close_characters[i]->player->team == -1 || col_intersect_line(f->pos, close_characters[i]->pos, NULL, NULL))
					continue;
				
				if(close_characters[i]->team == f->team)
				{
					// return the flag
					if(!f->at_stand)
					{
						CHARACTER *chr = close_characters[i];
						chr->player->score += 1;
						//Xp
						chr->player->xp += 20;

						dbg_msg("game", "flag_return player='%d:%s'",
							chr->player->client_id,
							server_clientname(chr->player->client_id));

						game.create_sound_global(SOUND_CTF_RETURN);
						f->reset();
					}
				}
				else
				{
					// take the flag
					if(f->at_stand)
					{
						teamscore[fi^1]++;
						f->grab_tick = server_tick();
					}
					f->at_stand = 0;
					f->carrying_character = close_characters[i];
					f->carrying_character->player->score += 1;
					//Xp
					f->carrying_character->player->xp += 10;

					dbg_msg("game", "flag_grab player='%d:%s'",
						f->carrying_character->player->client_id,
						server_clientname(f->carrying_character->player->client_id));
					
					for(int c = 0; c < MAX_CLIENTS; c++)
					{
						if(!game.players[c])
							continue;
							
						if(game.players[c]->team == fi)
							game.create_sound_global(SOUND_CTF_GRAB_EN, game.players[c]->client_id);
						else
							game.create_sound_global(SOUND_CTF_GRAB_PL, game.players[c]->client_id);
					}
					break;
				}
			}
			
			if(!f->carrying_character && !f->at_stand)
			{
				if(server_tick() > f->drop_tick + server_tickspeed()*30)
				{
					game.create_sound_global(SOUND_CTF_RETURN);
					f->reset();
				}
				else
				{
					f->vel.y += game.world.core.tuning.gravity;
					move_box(&f->pos, &f->vel, vec2(f->phys_size, f->phys_size), 0.5f);
				}
			}
		}
	}
}

bool GAMECONTROLLER_WAR::is_rpg() const
{
	return true;
}

//Level up stuff
void GAMECONTROLLER_WAR::on_level_up(PLAYER *player)
{
	if(player->race_name != VIDE && !player->levelmax && player->lvl < LVLMAX)
	{
		player->xp-=player->nextlvl;
		if(player->xp < 0)player->xp=0;
		player->lvl++;
		game.create_sound_global(SOUND_TEE_CRY, player->client_id);
		player->nextlvl = lvlmap[player->lvl];
		player->leveled++;
		if(player->lvl==LVLMAX)
			player->levelmax=true;
		display_level(player);
	}
	else if(player->race_name == VIDE && player->team != -1)
	{
		char buf[128];
		str_format(buf, sizeof(buf), "Please choose a race\n say \"/race name\"");
		game.send_broadcast(buf, player->client_id);
	}
	else if(!player->levelmax && player->lvl >= LVLMAX)
	{
		player->lvl=LVLMAX;
		game.create_sound_global(SOUND_TEE_CRY, player->client_id);
		player->levelmax=true;
		display_level(player);
	}
}


//Display level up information
void GAMECONTROLLER_WAR::display_level(PLAYER *player)
{
	char buf[128];
	if(player->lvl >= LVLMAX)
	{
		str_format(buf, sizeof(buf), "Final lvl");
	}
	else if(player->leveled)
	{
		str_format(buf, sizeof(buf), "LVL UP : (%d point(s) to spend)",player->leveled);
	}
	if(player->leveled)
	{
		if(player->race_name == ORC)
		{
			if(player->orc_dmg <3)
				strcat(buf,"\nsay \"/1\" Damage + 20%");
			if(player->orc_reload <3)
				strcat(buf,"\nsay \"/2\" Reload --");
			if(!player->orc_special && player->lvl>=6)
				strcat(buf,"\nsay \"/3\" SPECIAL : Teleport Backup");
			
		}
		else if(player->race_name == ELF)
		{
			if(player->elf_poison <3)
				strcat(buf,"\nsay \"/1\" Poison + 1");
			if(player->elf_mirror <3)
				strcat(buf,"\nsay \"/2\" Mirror damage + 1");
			if(!player->elf_special && player->lvl>=6)	
				strcat(buf,"\nsay \"/3\" SPECIAL : Immobilise");
		}
		else if(player->race_name == UNDEAD)
		{
			if(player->undead_taser <3)
				strcat(buf,"\nsay \"/1\" Taser ++");
			if(player->undead_vamp <3)
				strcat(buf,"\nsay \"/2\" Vampiric damage");
			if(!player->undead_special && player->lvl>=6)
				strcat(buf,"\nsay \"/3\" SPECIAL : Kamikaz");
		}
		else if(player->race_name == HUMAN)
		{
			if(player->human_armor <3)
				strcat(buf,"\nsay \"/1\" Armor + 20%");
			if(player->human_mole <3)
				strcat(buf,"\nsay \"/2\" Mole chance + 20%");
			if(!player->human_special && player->lvl>=6)
				strcat(buf,"\nsay \"/3\" SPECIAL : Teleport");
		}
	}

	game.send_broadcast(buf, player->client_id);
}

//Display current stats
void GAMECONTROLLER_WAR::display_stats(PLAYER *player,PLAYER *from)
{
	char buf[128];
	char tmp[128];
	if(player->client_id == from->client_id)
	{
		if(player->leveled)
		{
			display_level(player);
			return;
		}
		else
			str_format(buf, sizeof(buf), "Stats : (%d point to spend)",player->leveled);
	}
	else if(player->lvl >= LVLMAX)
		str_format(buf, sizeof(buf), "Final lvl Stats :");
	else
		str_format(buf, sizeof(buf), "Stats : ");
	if(player->race_name == ORC)
	{
		if(player->orc_dmg)
		{
			str_format(tmp,sizeof(tmp),"\n1 : Damage lvl %d/3",player->orc_dmg);
			strcat(buf,tmp);
		}
		if(player->orc_reload)
		{
			str_format(tmp,sizeof(tmp),"\n2 : Reload lvl %d/3",player->orc_reload);
			strcat(buf,tmp);
		}
		if(player->orc_special)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Teleport Backup");
			strcat(buf,tmp);
		}
		
	}
	else if(player->race_name == ELF)
	{
		if(player->elf_poison)
		{
			str_format(tmp,sizeof(tmp),"\n1 : Poison lvl %d/3",player->elf_poison);
			strcat(buf,tmp);
		}
		if(player->elf_mirror)
		{
			str_format(tmp,sizeof(tmp),"\n2 : Mirror damage lvl %d/3",player->elf_mirror);
			strcat(buf,tmp);
		}
		if(player->elf_special)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Immobilise");
			strcat(buf,tmp);
		}
	}
	else if(player->race_name == UNDEAD)
	{
		if(player->undead_taser)
		{
			str_format(tmp,sizeof(tmp),"\n1 : Taser lvl %d/3",player->undead_taser);
			strcat(buf,tmp);
		}
		if(player->undead_vamp)
		{
			str_format(tmp,sizeof(tmp),"\n2 : Vampiric damage lvl %d/3",player->undead_vamp);
			strcat(buf,tmp);
		}
		if(player->undead_special)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Kamikaz");
			strcat(buf,tmp);
		}
	}
	else if(player->race_name == HUMAN)
	{
		if(player->human_armor)
		{
			str_format(tmp,sizeof(tmp),"\n1 : Armor lvl %d/3",player->human_armor);
			strcat(buf,tmp);
		}
		if(player->human_mole)
		{
			str_format(tmp,sizeof(tmp),"\n2 : Mole chance lvl %d/3",player->human_mole);
			strcat(buf,tmp);
		}
		if(player->human_special)
		{
			str_format(tmp,sizeof(tmp),"\n3 : SPECIAL : Teleport");
			strcat(buf,tmp);
		}
	}
	game.send_broadcast(buf, from->client_id);
}	

//Fix orc flag
int GAMECONTROLLER_WAR::drop_flag_orc(PLAYER *player)
{
	FLAG *f = flags[!player->team];
	if(f && f->carrying_character && f->carrying_character == player->get_character())
	{
		game.create_sound_global(SOUND_CTF_DROP);
		f->drop_tick = server_tick();
		f->carrying_character = 0;
		f->vel = vec2(0,0);
		return -1;
	}
	else if(f && f->carrying_character && f->carrying_character != player->get_character())
		return f->carrying_character->player->client_id;
	return -1;
}

//Load custom xp table
void GAMECONTROLLER_WAR::load_xp_table()
{
		FILE *xptable;
		int i;
		xptable=fopen ("xp_table.war","r");
		if(xptable == NULL)
		{
			perror("Fopen (file exist ?)");
			return;
		}
		for(i=0;i < LVLMAX;i++)
		{
			fscanf(xptable,"%d\n",&lvlmap[i]);
		}
		fclose(xptable);
}

//Yes its ugly and ?
int GAMECONTROLLER_WAR::init_xp(int level)
{
	if(level < 1 || level > LVLMAX)
		return 0;
	return lvlmap[level];
}