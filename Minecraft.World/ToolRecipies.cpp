//package net.minecraft.world.item.crafting;

//import net.minecraft.world.item.*;
//import net.minecraft.world.level.tile.Tile;
#include "stdafx.h"
#include "net.minecraft.world.item.h"
#include "Tile.h"
#include "Recipy.h"
#include "Recipes.h"
#include "ToolRecipies.h"

// 4J-PB - adding "" on the end of these so we can detect it
wstring ToolRecipies::shapes[][4] = 
{
	{L"XXX", //
	L" # ",//
	L" # "},//

	{L"X",//
	L"#",//
	L"#"},//

	{L"XX",//
	L"X#",//
	L" #"},//

	{L"XX",//
	L" #",//
	L" #"},//
};

/*
	Object[][] map = {
			{Tile.wood, Tile.stoneBrick, Item.ironIngot, Item.diamond, Item.goldIngot}, 
			{Item.pickAxe_wood, Item.pickAxe_stone, Item.pickAxe_iron, Item.pickAxe_diamond, Item.pickAxe_gold}, 
			{Item.shovel_wood, Item.shovel_stone, Item.shovel_iron, Item.shovel_diamond, Item.shovel_gold}, 
			{Item.hatchet_wood, Item.hatchet_stone, Item.hatchet_iron, Item.hatchet_diamond, Item.hatchet_gold}, 
			{Item.hoe_wood, Item.hoe_stone, Item.hoe_iron, Item.hoe_diamond, Item.hoe_gold}, 
	};
	*/	
//#define ADD_OBJECT(a,b) a.push_back(new Object(b))

Item *ToolRecipies::handles[MAX_TOOL_HANDLES] = { NULL, NULL };

void ToolRecipies::_init()
{
	handles[0] = Item::stick;
	handles[1] = Item::skyrootStick;

	map = new vector <Object *> [MAX_TOOL_RECIPES];

	ADD_OBJECT(map[0],Tile::wood);
	ADD_OBJECT(map[0],Tile::stoneBrick);
	ADD_OBJECT(map[0],Item::ironIngot);
	ADD_OBJECT(map[0],Item::diamond);
	ADD_OBJECT(map[0],Item::goldIngot);
	// Aether
	ADD_OBJECT(map[0],Tile::skyrootPlanks);
	ADD_OBJECT(map[0],Tile::holystone);
	ADD_OBJECT(map[0],Item::zaniteGemstone);
	ADD_OBJECT(map[0],Item::gravititePlate);

	ADD_OBJECT(map[1],Item::pickAxe_wood);
	ADD_OBJECT(map[1],Item::pickAxe_stone);
	ADD_OBJECT(map[1],Item::pickAxe_iron);
	ADD_OBJECT(map[1],Item::pickAxe_diamond);
	ADD_OBJECT(map[1],Item::pickAxe_gold);
	// Aether
	ADD_OBJECT(map[1],Item::pickAxe_skyroot);
	ADD_OBJECT(map[1],Item::pickAxe_holystone);
	ADD_OBJECT(map[1],Item::pickAxe_zanite);
	ADD_OBJECT(map[1],Item::pickAxe_gravitite);

	ADD_OBJECT(map[2],Item::shovel_wood);
	ADD_OBJECT(map[2],Item::shovel_stone);
	ADD_OBJECT(map[2],Item::shovel_iron);
	ADD_OBJECT(map[2],Item::shovel_diamond);
	ADD_OBJECT(map[2],Item::shovel_gold);
	// Aether
	ADD_OBJECT(map[2],Item::shovel_skyroot);
	ADD_OBJECT(map[2],Item::shovel_holystone);
	ADD_OBJECT(map[2],Item::shovel_zanite);
	ADD_OBJECT(map[2],Item::shovel_gravitite);

	ADD_OBJECT(map[3],Item::hatchet_wood);
	ADD_OBJECT(map[3],Item::hatchet_stone);
	ADD_OBJECT(map[3],Item::hatchet_iron);
	ADD_OBJECT(map[3],Item::hatchet_diamond);
	ADD_OBJECT(map[3],Item::hatchet_gold);
	// Aether
	ADD_OBJECT(map[3],Item::hatchet_skyroot);
	ADD_OBJECT(map[3],Item::hatchet_holystone);
	ADD_OBJECT(map[3],Item::hatchet_zanite);
	ADD_OBJECT(map[3],Item::hatchet_gravitite);

	ADD_OBJECT(map[4],Item::hoe_wood);
	ADD_OBJECT(map[4],Item::hoe_stone);
	ADD_OBJECT(map[4],Item::hoe_iron);
	ADD_OBJECT(map[4],Item::hoe_diamond);
	ADD_OBJECT(map[4],Item::hoe_gold);
	// Aether (no hoes)
	map[4].push_back(NULL);
	map[4].push_back(NULL);
	map[4].push_back(NULL);
	map[4].push_back(NULL);
}

void ToolRecipies::addRecipes(Recipes *r) 
{
	wchar_t wchTypes[7];
	wchTypes[6]=0;

	for (int h = 0; h < MAX_TOOL_HANDLES; h++)
	{
		for (unsigned int m = 0; m < map[0].size(); m++) 
		{
			Object *pObjMaterial = map[0].at(m);

			for (int t=0; t<MAX_TOOL_RECIPES-1; t++) 
			{
				Object *pObjTarget = map[t+1].at(m);
				if (pObjTarget == NULL) continue;
				Item *target = pObjTarget->item;

				wchTypes[0]=L'w';
				wchTypes[1]=L'c';
				wchTypes[2]=L'i';
				wchTypes[3]=L'c';
				wchTypes[5]=L'g';
				if(pObjMaterial->GetType()==eType_TILE)
				{
					wchTypes[4]=L't';
					r->addShapedRecipy(new ItemInstance(target), 
						wchTypes,
						shapes[t], 

						L'#', handles[h],
						L'X', pObjMaterial->tile,
						L'T');
				}
				else
				{
					// must be Item
					wchTypes[4]=L'i';
					r->addShapedRecipy(new ItemInstance(target), 
						wchTypes,
						shapes[t], 

						L'#', handles[h],
						L'X', pObjMaterial->item,
						L'T');
				}
			}
		}
	}
	r->addShapedRecipy(new ItemInstance((Item *)Item::shears), 
		L"sscig",
        L" #", //
        L"# ", //
		L'#', Item::ironIngot,
		L'T'
		);
}