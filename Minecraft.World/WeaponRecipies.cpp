#include "stdafx.h"

#include "net.minecraft.world.item.h"
#include "Tile.h"
#include "Recipy.h"
#include "Recipes.h"
#include "WeaponRecipies.h"

// 4J-PB - adding "" on the end of these so we can detect it
wstring WeaponRecipies::shapes[][4] = 
{
			{L"X", //
			 L"X",//
			 L"#",L""},//
};
		   
/*
	private Object[][] map = {
			{Tile.wood, Tile.stoneBrick, Item.ironIngot, Item.diamond, Item.goldIngot}, 
			{Item.sword_wood, Item.sword_stone, Item.sword_iron, Item.sword_diamond, Item.sword_gold}, 
	};
*/

Item *WeaponRecipies::handles[MAX_WEAPON_HANDLES] = { NULL, NULL };

void WeaponRecipies::_init()
{
	handles[0] = Item::stick;
	handles[1] = Item::skyrootStick;

	map = new vector <Object *> [MAX_WEAPON_RECIPES];

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

	ADD_OBJECT(map[1],Item::sword_wood);
	ADD_OBJECT(map[1],Item::sword_stone);
	ADD_OBJECT(map[1],Item::sword_iron);
	ADD_OBJECT(map[1],Item::sword_diamond);
	ADD_OBJECT(map[1],Item::sword_gold);
	// Aether
	ADD_OBJECT(map[1],Item::sword_skyroot);
	ADD_OBJECT(map[1],Item::sword_holystone);
	ADD_OBJECT(map[1],Item::sword_zanite);
	ADD_OBJECT(map[1],Item::sword_gravitite);
}

void WeaponRecipies::addRecipes(Recipes *r) 
{
	wchar_t wchTypes[7];
	wchTypes[6]=0;

	for (int h = 0; h < MAX_WEAPON_HANDLES; h++)
	{
		for (unsigned int m = 0; m < map[0].size(); m++) 
		{
			Object *pObjMaterial = map[0].at(m);

			for (int t=0; t<MAX_WEAPON_RECIPES-1; t++) 
			{
				Item *target = map[t+1].at(m)->item;

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

	/* 4J-PB - moved out to main recipes so we can avoid them stacking on the group display name
	r->addShapedRecipy(new ItemInstance(Item::bow, 1), //
		L"ssscicig",
		L" #X", //
		L"# X", //
		L" #X", //

		L'X', Item::string,// 
		L'#', Item::stick,
		L'T');

	r->addShapedRecipy(new ItemInstance(Item::arrow, 4), //
		L"ssscicicig",
		L"X", //
		L"#", //
		L"Y", //

		L'Y', Item::feather,// 
		L'X', Item::flint,// 
		L'#', Item::stick,
		L'T');
		*/
}