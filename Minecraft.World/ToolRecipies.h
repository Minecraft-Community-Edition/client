//package net.minecraft.world.item.crafting;

//import net.minecraft.world.item.*;
//import net.minecraft.world.level.tile.Tile;
#pragma once

#define MAX_TOOL_RECIPES 5
#define MAX_TOOL_HANDLES 2

class Recipes;

class ToolRecipies 
{
public:
	// 4J - added for common ctor code
	void _init();
	ToolRecipies()			{_init();}

private:
	static wstring shapes[][4]; 
	vector <Object *> *map;
	static Item *handles[MAX_TOOL_HANDLES];

public:
	void addRecipes(Recipes *r); 
};
