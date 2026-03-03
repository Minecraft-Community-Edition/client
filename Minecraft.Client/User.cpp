#include "stdafx.h"
#include "User.h"
#include "..\Minecraft.World\net.minecraft.world.level.tile.h"

vector<Tile *> User::allowedTiles;

void User::staticCtor()
{
    allowedTiles.push_back(Tile::rock);
    allowedTiles.push_back(Tile::stoneBrick);
    allowedTiles.push_back(Tile::redBrick);
    allowedTiles.push_back(Tile::dirt);
    allowedTiles.push_back(Tile::wood);
    allowedTiles.push_back(Tile::treeTrunk);
    allowedTiles.push_back(Tile::leaves);
    allowedTiles.push_back(Tile::torch);
    allowedTiles.push_back(Tile::stoneSlabHalf);

    allowedTiles.push_back(Tile::glass);
    allowedTiles.push_back(Tile::mossStone);
    allowedTiles.push_back(Tile::sapling);
    allowedTiles.push_back(Tile::flower);
    allowedTiles.push_back(Tile::rose);
    allowedTiles.push_back(Tile::mushroom1);
    allowedTiles.push_back(Tile::mushroom2);
    allowedTiles.push_back(Tile::sand);
    allowedTiles.push_back(Tile::gravel);
    allowedTiles.push_back(Tile::sponge);

    allowedTiles.push_back(Tile::cloth);
    allowedTiles.push_back(Tile::coalOre);
    allowedTiles.push_back(Tile::ironOre);
    allowedTiles.push_back(Tile::goldOre);
    allowedTiles.push_back(Tile::ironBlock);
    allowedTiles.push_back(Tile::goldBlock);
    allowedTiles.push_back(Tile::bookshelf);
    allowedTiles.push_back(Tile::tnt);
    allowedTiles.push_back(Tile::obsidian);

	// Aether Blocks
	allowedTiles.push_back(Tile::aetherGrass);
	allowedTiles.push_back(Tile::aetherDirt);
	allowedTiles.push_back(Tile::holystone);
	allowedTiles.push_back(Tile::mossyHolystone);
	allowedTiles.push_back(Tile::ambrosiumOre);
	allowedTiles.push_back(Tile::zaniteOre);
	allowedTiles.push_back(Tile::gravititeOre);
	allowedTiles.push_back(Tile::skyrootLog);
	allowedTiles.push_back(Tile::skyrootPlanks);
	allowedTiles.push_back(Tile::skyrootLeaves);
	allowedTiles.push_back(Tile::goldenOakLog);
	allowedTiles.push_back(Tile::goldenOakLeaves);
	allowedTiles.push_back(Tile::quicksoil);
	allowedTiles.push_back(Tile::quicksoilGlass);
	allowedTiles.push_back(Tile::aercloud);
	allowedTiles.push_back(Tile::aerogel);
	allowedTiles.push_back(Tile::icestone);
	allowedTiles.push_back(Tile::ambrosiumTorch);
	allowedTiles.push_back(Tile::skyrootSapling);
	allowedTiles.push_back(Tile::goldenOakSapling);
	allowedTiles.push_back(Tile::purpleFlower);
	allowedTiles.push_back(Tile::whiteFlower);
	allowedTiles.push_back(Tile::angelicStone);
	allowedTiles.push_back(Tile::lightAngelicStone);
	allowedTiles.push_back(Tile::carvedStone);
	allowedTiles.push_back(Tile::lightCarvedStone);
	allowedTiles.push_back(Tile::hellfireStone);
	allowedTiles.push_back(Tile::lightHellfireStone);
	allowedTiles.push_back(Tile::pillarTop);
	allowedTiles.push_back(Tile::pillarCarved);
	allowedTiles.push_back(Tile::enchanter);
	allowedTiles.push_back(Tile::freezer);
	allowedTiles.push_back(Tile::incubator);
	allowedTiles.push_back(Tile::libraryLore);
}

User::User(const wstring& name, const wstring& sessionId)
{
    this->name = name;
    this->sessionId = sessionId;
}
